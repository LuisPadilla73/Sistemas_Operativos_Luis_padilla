
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dac.h>

#define FS_HZ             5000.0f

/* Periodo del timer en microsegundos (1 / 8 kHz = 125 us) */
#define SAMPLE_PERIOD_US  ((uint32_t)(1000000u / (uint32_t)FS_HZ))

/* Tamaño de bloque (frames) */
#define FRAME_SIZE        64
#define QUEUE_DEPTH       4   /* profundidad de colas */
/* params de autotune*/
#define PITCH_ALPHA   1.0f
#define CORR_STRENGTH 1.0f

/* ===================== Devicetree: ADC ===================== */

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#if !DT_NODE_EXISTS(ZEPHYR_USER_NODE)
#error "Falta el nodo /zephyr,user en el devicetree"
#endif

#if !DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, io_channels)
#error "/zephyr,user debe tener io-channels para el ADC"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(ZEPHYR_USER_NODE, io_channels,
			     DT_SPEC_AND_COMMA)
};

/* ===================== Devicetree: DAC ===================== */

#if (DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, dac) && \
     DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, dac_channel_id) && \
     DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, dac_resolution))

#define DAC_NODE        DT_PHANDLE(ZEPHYR_USER_NODE, dac)
#define DAC_CHANNEL_ID  DT_PROP(ZEPHYR_USER_NODE, dac_channel_id)
#define DAC_RESOLUTION  DT_PROP(ZEPHYR_USER_NODE, dac_resolution)

#else
#error "/zephyr,user debe tener dac, dac_channel_id y dac_resolution"
#endif

static const struct device *const dac_dev = DEVICE_DT_GET(DAC_NODE);

static const struct dac_channel_cfg dac_ch_cfg = {
	.channel_id  = DAC_CHANNEL_ID,
	.resolution  = DAC_RESOLUTION,
#if defined(CONFIG_DAC_BUFFER_NOT_SUPPORT)
	.buffered = false,
#else
	.buffered = true,
#endif
};

#define ADC_BITS      12
#define ADC_MAX_F     ((float)((1u << ADC_BITS) - 1u))  /* 4095 */
#define ADC_MID_F     (ADC_MAX_F / 2.0f)

#define DAC_BITS      DAC_RESOLUTION
#define DAC_MAX_F     ((float)((1u << DAC_BITS) - 1u))


struct audio_block {
	uint16_t data[FRAME_SIZE];
};

K_MSGQ_DEFINE(adc_to_dsp_q, sizeof(struct audio_block), QUEUE_DEPTH, 4);
K_MSGQ_DEFINE(dsp_to_io_q,  sizeof(struct audio_block), QUEUE_DEPTH, 4);

/* Semáforo de sampleo: timer da, audio_io_thread toma */
K_SEM_DEFINE(sample_sem, 0, 1);

/* ===================== AUTOTUNE: detector de pitch + pitch shifter ===================== */

/* Rango de F0 (voz humana aprox) */
#define F0_MIN_HZ       80.0f
#define F0_MAX_HZ       600.0f

/* Tamaño de ventana para pitch (en muestras) */
#define PITCH_BUF_SIZE        256   /* 256/8000 = 32 ms de ventana */

/* Cada cuántas muestras actualizamos */
#define PITCH_UPDATE_SAMPLES  264

/* Umbral de energía para decidir si hay señal útil o silencio */
#define ENERGY_THRESHOLD      0.01f

/* Buffer circular para detección de pitch */
static float pitch_buf[PITCH_BUF_SIZE];
static int   pitch_index = 0;
static float current_f0_hz = 0.0f;

/* Buffer circular para pitch shifter */
#define PROC_BUF_SIZE   2048
static float proc_buf[PROC_BUF_SIZE];
static int   write_pos = 0;
static float read_pos  = 0.0f;

/* Factor de pitch actual (1.0 = sin cambio) */
static float pitch_factor = 0.5f;

static inline float fast_fabsf(float x)
{
    return (x >= 0.0f) ? x : -x;
}

//notas bien marcadas 
/* const float note_freqs[] = {
    196.00f,  // Sol
    220.00f,  // La
    246.94f,  // Si
    261.63f,  // Do
    293.66f,  // Re
    329.63f,  // Mi
    349.23f   // Fa
};
 */
static const float note_freqs[] = {
    // --- E2 a D3 ---
    82.41f,   // E2
    87.31f,   // F2
    92.50f,   // F#2 / Gb2
    98.00f,   // G2
    103.83f,  // G#2 / Ab2
    110.00f,  // A2
    116.54f,  // A#2 / Bb2
    123.47f,  // B2
    130.81f,  // C3
    138.59f,  // C#3 / Db3
    146.83f,  // D3
    155.56f,  // D#3 / Eb3

    // --- E3 a D4 ---
    164.81f,  // E3
    174.61f,  // F3
    185.00f,  // F#3 / Gb3
    196.00f,  // G3
    207.65f,  // G#3 / Ab3
    220.00f,  // A3
    233.08f,  // A#3 / Bb3
    246.94f,  // B3
    261.63f,  // C4
    277.18f,  // C#4 / Db4
    293.66f,  // D4
    311.13f,  // D#4 / Eb4

    // --- E4 a D5 ---
    329.63f,  // E4
    349.23f,  // F4
    369.99f,  // F#4 / Gb4
    392.00f,  // G4
    415.30f,  // G#4 / Ab4
    440.00f,  // A4
    466.16f,  // A#4 / Bb4
    493.88f,  // B4
    523.25f,  // C5
    554.37f,  // C#5 / Db5
    587.33f   // D5
};


#define NUM_NOTES   (sizeof(note_freqs) / sizeof(note_freqs[0]))

/* ---- Actualizar buffer de pitch con la señal de voz---- */
static inline void update_pitch_buffer(float sample)
{
	pitch_buf[pitch_index] = sample;
	pitch_index++;
	if (pitch_index >= PITCH_BUF_SIZE) {
		pitch_index = 0;
	}
}

/* ---- Estimar F0 por autocorrelación ---- */
static void estimate_pitch(void)
{
	float x[PITCH_BUF_SIZE];
	int idx = pitch_index;
	for (int i = 0; i < PITCH_BUF_SIZE; i++) {
		x[i] = pitch_buf[idx];
		idx++;
		if (idx >= PITCH_BUF_SIZE) {
			idx = 0;
		}
	}

	/* 2) Quitar la media (DC) */
	float mean = 0.0f;
	for (int i = 0; i < PITCH_BUF_SIZE; i++) {
		mean += x[i];
	}
	mean /= (float)PITCH_BUF_SIZE;
	for (int i = 0; i < PITCH_BUF_SIZE; i++) {
		x[i] -= mean;
	}

	/* 3) Checar energía para evitar detectar pitch en silencio */
	float energy = 0.0f;
	for (int i = 0; i < PITCH_BUF_SIZE; i++) {
		energy += x[i] * x[i];
	}
	if (energy < ENERGY_THRESHOLD) {
		current_f0_hz = 0.0f;
		return;
	}

	/*Rango de muestras correspondientes a los params de la voz*/
	int lag_min = (int)(FS_HZ / F0_MAX_HZ);
	int lag_max = (int)(FS_HZ / F0_MIN_HZ);
	if (lag_max >= PITCH_BUF_SIZE) {
		lag_max = PITCH_BUF_SIZE - 1;
	}

	float best_r = 0.0f;
	int   best_lag = lag_min;

	/* Autocorrelación, 
	detecta que tanto se parecen, se tiene que elegir el retardo que maximice la similitud */
	for (int lag = lag_min; lag <= lag_max; lag++) {
		float r = 0.0f;
		for (int n = 0; n < PITCH_BUF_SIZE - lag; n++) {
			r += x[n] * x[n + lag];
		}
		if (r > best_r) {
			best_r   = r;
			best_lag = lag;
		}
	}

	/* Convertir muestras a la  F0 */
	if (best_r > 0.0f) {
		current_f0_hz = FS_HZ / (float)best_lag;
	} else {
		current_f0_hz = 0.0f;
	}
}

bool silence = false;

static void update_pitch_factor(void)
{
    float f0 = current_f0_hz;

    if (f0 <= 0.0f) {
        pitch_factor = 1.0f;
		silence = true;
        return;
    }else{
		silence = false;
	}

    /* Buscar la nota más cercana en note_freqs dependiendo de la f0 */
    float best_freq = note_freqs[0];
    float best_err  = fast_fabsf(f0 - best_freq); //queremos el error más cercano a 0 para encontrar la nota más parecida con su abs

    for (int i = 1; i < (int)NUM_NOTES; i++) {//encontrar el error menor dentro de todo el arreglo 
        float err = fast_fabsf(f0 - note_freqs[i]);
        if (err < best_err) {
            best_err  = err;
            best_freq = note_freqs[i];
        }
    }
	/* uint32_t debugf0 = f0*1000;
	uint32_t debugbest = best_freq*1000;


	printf("f0: %d mHz best_freq: %d mHz",debugf0,debugbest);
     */
    float ideal_factor = best_freq / f0; //ptch necesario 

    //que tanto se corrige
    float new_factor = 1.0f + CORR_STRENGTH * (ideal_factor - 1.0f);//que tan fuerte se va a aplicar la corr
	
    pitch_factor = (1.0f - PITCH_ALPHA) * pitch_factor
                 + PITCH_ALPHA * new_factor;//que tan  rapido salta,se aplica el pitch 
}

static inline float pitchshift_process_sample(float x_in)
{
	/* 1) Escribir la muestra en el buffer circular */
	proc_buf[write_pos] = x_in;
	write_pos++;
	if (write_pos >= PROC_BUF_SIZE) {
		write_pos = 0;
	}

	/* 2) Leer con índice fraccional */
	int   idx0 = (int)read_pos;
	int   idx1 = idx0 + 1;
	if (idx1 >= PROC_BUF_SIZE) {
		idx1 = 0;
	}

	float frac = read_pos - (float)idx0;
	float s0   = proc_buf[idx0];
	float s1   = proc_buf[idx1];

	float y = s0 + frac * (s1 - s0);  //simular continuidad 

	/* 3) Avanzar read_pos según pitch_factor para hacer maás grave o más agudo */
	float step = 1.0f / pitch_factor;
	read_pos += step;

	if (read_pos >= (float)PROC_BUF_SIZE) {
		read_pos -= (float)PROC_BUF_SIZE;
	} else if (read_pos < 0.0f) {
		read_pos += (float)PROC_BUF_SIZE;
	}

	return y;
}


static void sample_timer_cb(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_sem_give(&sample_sem); //semafor al muestreo que queremos 
}

K_TIMER_DEFINE(sample_timer, sample_timer_cb, NULL);


#define AUDIO_IO_STACK_SIZE  2048
#define AUDIO_IO_PRIORITY    0   

K_THREAD_STACK_DEFINE(audio_io_stack, AUDIO_IO_STACK_SIZE);
static struct k_thread audio_io_thread_data;

/* Hilo de audio IO, más priodiad */
static void audio_io_thread(void *p1, void *p2, void *p3)
{
	struct audio_block in_block;
	struct audio_block out_block;
	int in_index  = 0;
	int out_index = 0;

	for (int i = 0; i < FRAME_SIZE; i++) {
		out_block.data[i] = (uint16_t)(DAC_MAX_F / 2.0f);
	}
	uint16_t adc_raw;
	struct adc_sequence seq = {
		.buffer      = &adc_raw,
		.buffer_size = sizeof(adc_raw),
	};

	while (1) {
		k_sem_take(&sample_sem, K_FOREVER);

		(void)adc_sequence_init_dt(&adc_channels[0], &seq);
		int err = adc_read_dt(&adc_channels[0], &seq);
		if (err < 0) {
			adc_raw = (uint16_t)ADC_MID_F; 
		}

		in_block.data[in_index++] = adc_raw;

		if (in_index >= FRAME_SIZE) {
			(void)k_msgq_put(&adc_to_dsp_q, &in_block, K_NO_WAIT);
			in_index = 0;
		}
		uint16_t dac_code = out_block.data[out_index++];

		if (out_index >= FRAME_SIZE) {
			struct audio_block new_block;
			if (k_msgq_get(&dsp_to_io_q, &new_block, K_NO_WAIT) == 0) {
				out_block = new_block;
				out_index = 0;
				dac_code  = out_block.data[out_index++];
			} else {
				/* for (int i = 0; i < FRAME_SIZE; i++) {
					out_block.data[i] = adc_raw;
				} */
				/* float x = ((float)adc_raw - ADC_MID_F) / ADC_MID_F;
				float dac_f = (x * 0.5f + 0.5f) * DAC_MAX_F;
				out_block = 
				dac_code    = (uint16_t)(dac_f + 0.5f);
				*/
				out_index = 0; 
			}
			
			
		}

		(void)dac_write_value(dac_dev, DAC_CHANNEL_ID, dac_code);
	}
}

/* ===================== Hilo de AUTOTUNE ===================== */

#define DSP_STACK_SIZE   4096
#define DSP_PRIORITY     1   /* procesamiento */

K_THREAD_STACK_DEFINE(dsp_stack, DSP_STACK_SIZE);
static struct k_thread dsp_thread_data;

static void dsp_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct audio_block in_block;
	struct audio_block out_block;
	uint32_t global_sample_count = 0;
	float y_autotune;

	while (1) {
		/* Esperar un bloque desde el hilo de IO a procesar */
		k_msgq_get(&adc_to_dsp_q, &in_block, K_FOREVER);

		for (int n = 0; n < FRAME_SIZE; n++) {
			/* 1) Pasar ADC (0..4095) a float aprox -1..1 */
			float x = ((float)in_block.data[n] - ADC_MID_F) / ADC_MID_F;

			/* 2) Actualizar buffer de pitch (rama análisis) */
			update_pitch_buffer(x);

			/* 3) Pitch shifter: aplicar factor a la señal directa */
			if(silence){
				pitch_factor        = 1.0f;
				read_pos            = write_pos;  // alineas lectura con escritura
				y_autotune = x; 
			}else{
				y_autotune = pitchshift_process_sample(x);


			}

			global_sample_count++; //numero de samples para actualizar el pitch 
			if ((global_sample_count % PITCH_UPDATE_SAMPLES) == 0U) {
				estimate_pitch();
				update_pitch_factor();
			}
			//escalar para el dac
			if (y_autotune >  0.999f) y_autotune =  0.999f;
			if (y_autotune < -0.999f) y_autotune = -0.999f;

			float dac_f       = (y_autotune * 0.5f + 0.5f) * DAC_MAX_F;
			uint16_t dac_code = (uint16_t)(dac_f + 0.5f);
			out_block.data[n] = dac_code;
		}

		k_msgq_put(&dsp_to_io_q, &out_block, K_FOREVER); //manda el bloque para el dac
	}
}

#include <zephyr/sys/util.h>


extern uint32_t SystemCoreClock;

int main(void)
{
	int err;
	printk("SystemCoreClock = %u Hz\n", SystemCoreClock);
	/* Inicializar buffers de autotune */
	for (int i = 0; i < PITCH_BUF_SIZE; i++) {
		pitch_buf[i] = 0.0f;
	}
	for (int i = 0; i < PROC_BUF_SIZE; i++) {
		proc_buf[i] = 0.0f;
	}
	pitch_index   = 0;
	write_pos     = 0;
	read_pos      = 0.0f;
	pitch_factor  = 1.0f;
	current_f0_hz = 0.0f;

	/* ----- Inicializar ADC ----- */
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!adc_is_ready_dt(&adc_channels[i])) {
			printk("ADC controlador %s no listo\n",
			       adc_channels[i].dev->name);
			return 0;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			printk("Fallo setup canal ADC #%d (%d)\n",
			       (int)i, err);
			return 0;
		}
	}

	printk("ADC listo: %s, canal %d\n",
	       adc_channels[0].dev->name,
	       adc_channels[0].channel_id);

	/* ----- Inicializar DAC ----- */
	if (!device_is_ready(dac_dev)) {
		printk("DAC device %s no está listo\n", dac_dev->name);
		return 0;
	}

	err = dac_channel_setup(dac_dev, &dac_ch_cfg);
	if (err != 0) {
		printk("Fallo en dac_channel_setup: %d\n", err);
		return 0;
	}

	printk("DAC listo: %s, canal %d, resolución %d bits\n",
	       dac_dev->name, DAC_CHANNEL_ID, DAC_RESOLUTION);

	/* ----- Arrancar timer de muestreo a ~8 kHz ----- */
	printk("Iniciando k_timer de muestreo cada %u us (~%.1f Hz)\n",
	       SAMPLE_PERIOD_US, (double)FS_HZ);

	k_timer_start(&sample_timer,
	              K_NO_WAIT,
	              K_USEC(SAMPLE_PERIOD_US));

	/* ----- Crear hilos de AUDIO IO y DSP ----- */
	printk("Creando hilos de audio_io y dsp...\n");

	k_thread_create(&audio_io_thread_data,
	                audio_io_stack,
	                K_THREAD_STACK_SIZEOF(audio_io_stack),
	                audio_io_thread,
	                NULL, NULL, NULL,
	                AUDIO_IO_PRIORITY,
	                0,
	                K_NO_WAIT);

	k_thread_create(&dsp_thread_data,
	                dsp_stack,
	                K_THREAD_STACK_SIZEOF(dsp_stack),
	                dsp_thread,
	                NULL, NULL, NULL,
	                DSP_PRIORITY,
	                0,
	                K_NO_WAIT);

	
	while (1) {
		k_sleep(K_SECONDS(5));
	
	}
}
