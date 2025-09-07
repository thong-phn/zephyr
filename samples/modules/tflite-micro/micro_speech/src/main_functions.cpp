#include "main_functions.hpp"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(micro_speech_openamp, LOG_LEVEL_DBG);

#include "inference/model_runner.hpp"
#include "transport/rpmsg_transport.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Stack sizes threads */
#define APP_RECEIVE_TASK_STACK_SIZE (2048)
#define APP_AUDIO_PROCESSING_TASK_STACK_SIZE (4096)

/* Audio pipeline */
#define SAMPLE_SIZE_BYTES (2)
#define ML_BUFFER_SIZE_SAMPLES (16000)

#define FRAME_SIZE_SAMPLES (320)					// 1 frame = 20ms = 320 samples
#define FRAME_SIZE_BYTES (FRAME_SIZE_SAMPLES * 2) 	// 1 frame = 640 bytes
#define FRAMES_PER_BUFFER (10)						// 12 frames per buffer
#define BUFFER_SIZE_BYTES (FRAMES_PER_BUFFER * FRAME_SIZE_BYTES) // 1 buffer = 24 frames = 7680 bytes = 240ms
#define BUFFER_SIZE_SAMPLES (FRAMES_PER_BUFFER * FRAME_SIZE_SAMPLES)

static uint8_t buffer_a[BUFFER_SIZE_BYTES];
static uint8_t buffer_b[BUFFER_SIZE_BYTES];
static uint8_t *write_buffer = buffer_a;
static uint8_t *processing_buffer = buffer_b;

static int16_t ml_buffer[ML_BUFFER_SIZE_SAMPLES];

static uint16_t g_samples_in_write_buffer = 0;
static uint16_t g_samples_in_processing_buffer = 0;
static bool g_eof_status = false;
static int8_t count = 0;

/* Thread stacks */
K_THREAD_STACK_DEFINE(thread_receive_stack, APP_RECEIVE_TASK_STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_audio_processing_stack, APP_AUDIO_PROCESSING_TASK_STACK_SIZE);
static struct k_thread thread_receive_data;
static struct k_thread thread_audio_processing_data;

/* Inter-thread communication for audio processing */
static K_SEM_DEFINE(processing_buffer_ready_sem, 0, 1);
static K_SEM_DEFINE(ml_complete_sem, 1, 1); // Start with 1 to indicate ready
static K_MUTEX_DEFINE(buffer_access_mutex);

/* --- Function Implementations --- */
extern "C" {
void app_receive_data_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	char debug_buff[496];
	
	// [DEBUG] Print
	LOG_INF("Audio Aggregator thread started (Priority: 6)");	
	
	// Wait for rpmsg ready before creating endpoint
	k_sem_take(&data_tty_ready_sem, K_FOREVER);
	
	// Create endpoint for frame reception
	tty_ept.priv = NULL;
	int ret = rpmsg_create_ept(&tty_ept, rpdev, "rpmsg-tty", RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, rpmsg_recv_tty_callback, NULL);
	if (ret) {
		LOG_ERR("Could not create endpoint: %d", ret);
		goto task_end;
	}

	while (tty_ept.addr != RPMSG_ADDR_ANY) {
		// Get msg from queue
		struct rpmsg_rcv_msg rx_msg;
		k_msgq_get(&tty_msgq, &rx_msg, K_FOREVER);
		
		if (rx_msg.len) {
			if (rx_msg.len == 3 && memcmp(rx_msg.data, "EOF", 3) == 0) { // Check for EOF
				// [DEBUG] Print
				snprintf(debug_buff, sizeof(debug_buff), "[Z] EOF received\n");
				rpmsg_send(&tty_ept, debug_buff, strlen(debug_buff));

				// Process remaining data
				if (g_samples_in_write_buffer > 0) {
					k_mutex_lock(&buffer_access_mutex, K_FOREVER);

					// Reset sem to 0
					k_sem_reset(&ml_complete_sem);

					// Swap buffers 
					uint8_t* temp = write_buffer;
					write_buffer = processing_buffer;
					processing_buffer = temp;

					// Samples counter for processing buffer
					g_samples_in_processing_buffer = g_samples_in_write_buffer;
					
					// Mark EOF
					g_eof_status = true;

					// Notice processing_buffer is ready
					k_sem_give(&processing_buffer_ready_sem);
					k_mutex_unlock(&buffer_access_mutex);
					
					// Wait for micro-speech to complete before resetting. Avoid race condition
					int ret = k_sem_take(&ml_complete_sem, K_MSEC(5000));
					if (ret != 0) {
						LOG_WRN("ML processing timeout during EOF");
						k_sem_reset(&ml_complete_sem);
					}
				} 

				// Reset frame counter for write buffer
				g_samples_in_write_buffer = 0;
			} else {// Process new frame
				k_mutex_lock(&buffer_access_mutex, K_FOREVER);
				
				// Check available space for the new frame
				// If yes, copy to the write buffer, check buffer status, notice micro-speech
				// If no, drop frame due to buffer overflow 
				if (g_samples_in_write_buffer < BUFFER_SIZE_SAMPLES) {
					// [DEBUG] Print
					if (g_samples_in_write_buffer == 3200) {
						snprintf(debug_buff, sizeof(debug_buff), "[Z] app_receive_data_thread: Receive %d samples\n", g_samples_in_write_buffer);
						rpmsg_send(&tty_ept, debug_buff, strlen(debug_buff));
					}

					size_t offset = g_samples_in_write_buffer * SAMPLE_SIZE_BYTES;
					memcpy(&write_buffer[offset], rx_msg.data, rx_msg.len);
					g_samples_in_write_buffer += (rx_msg.len / SAMPLE_SIZE_BYTES);
					
					// Check buffer is full or not
					if (g_samples_in_write_buffer >= BUFFER_SIZE_SAMPLES) {						

						// Swap buffer
						uint8_t* temp = write_buffer;
						write_buffer = processing_buffer;
						processing_buffer = temp;

						// Samples counter for processing buffer
						g_samples_in_processing_buffer = BUFFER_SIZE_SAMPLES;
						g_samples_in_write_buffer = 0; // Reset

						// Notice micro-speech
						k_sem_give(&processing_buffer_ready_sem);
					} 
				} else {
					LOG_WRN("Write buffer full, dropping samples %u", g_samples_in_write_buffer);
					// [DEBUG] Print
					snprintf(debug_buff, sizeof(debug_buff), "[Z] app_receive_data_thread: Dropping %d samples\n", g_samples_in_write_buffer);
					rpmsg_send(&tty_ept, debug_buff, strlen(debug_buff));
				}
				k_mutex_unlock(&buffer_access_mutex);
			}
			
			// Release the queue after processing
			rpmsg_release_rx_buffer(&tty_ept, rx_msg.data);
		}
	}
	
	// Clean up
	rpmsg_destroy_ept(&tty_ept);

task_end:
	LOG_INF("[Z] Audio Aggregator thread ended");
	snprintf(debug_buff, sizeof(debug_buff), "[Z] Audio Aggregator thread ended\n");
	rpmsg_send(&tty_ept, debug_buff, strlen(debug_buff));
    
}

void app_audio_processing_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    char debug_buff[496];
    
    // 1-second buffer for ML inference (16000 samples * 2 bytes/sample)
    constexpr size_t ml_buffer_bytes = ML_BUFFER_SIZE_SAMPLES * SAMPLE_SIZE_BYTES;
    size_t samples_in_ml_buffer = 0;

    LOG_INF("Audio Processing thread started (Priority: 7)");

    while (1) {
        // Wait for audio data to be ready
        k_sem_take(&processing_buffer_ready_sem, K_FOREVER);
        
        // New data has arrived in processing_buffer (200ms = 3200 samples)
		k_mutex_lock(&buffer_access_mutex, K_FOREVER);
        size_t new_samples = g_samples_in_processing_buffer;
        size_t new_bytes = new_samples * SAMPLE_SIZE_BYTES;
		k_mutex_unlock(&buffer_access_mutex);

		// [DEBUG] Print
		snprintf(debug_buff, sizeof(debug_buff), "[Z] app_audio_processing_thread: Receive %d samples\n", new_samples);
		rpmsg_send(&tty_ept, debug_buff, strlen(debug_buff));

        
        if (samples_in_ml_buffer < ML_BUFFER_SIZE_SAMPLES) {// If the ML buffer is not full yet, just append the new data
            size_t samples_to_copy = MIN(new_samples, ML_BUFFER_SIZE_SAMPLES - samples_in_ml_buffer);
            size_t bytes_to_copy = samples_to_copy * SAMPLE_SIZE_BYTES;

			k_mutex_lock(&buffer_access_mutex, K_FOREVER);
            memcpy((uint8_t*)ml_buffer + (samples_in_ml_buffer * SAMPLE_SIZE_BYTES), processing_buffer, bytes_to_copy);
            samples_in_ml_buffer += samples_to_copy;
			k_mutex_unlock(&buffer_access_mutex);
        } else {// Buffer is full, implement sliding window     
            // 1. Shift old data to the left (make space for new data at the end)
            size_t shift_bytes = new_bytes;
            memmove(ml_buffer, (uint8_t*)ml_buffer + shift_bytes, ml_buffer_bytes - shift_bytes);

            // 2. Copy new data to the end of the buffer
			k_mutex_lock(&buffer_access_mutex, K_FOREVER);
            memcpy((uint8_t*)ml_buffer + (ml_buffer_bytes - new_bytes), processing_buffer, new_bytes);
			k_mutex_unlock(&buffer_access_mutex);
        }

        // Only run inference if the buffer is full
        if ((samples_in_ml_buffer >= ML_BUFFER_SIZE_SAMPLES) || g_eof_status) {
            int ret = micro_speech_process_audio(ml_buffer, samples_in_ml_buffer);
            if (ret != 0) {
                LOG_ERR("micro_speech_process_audio error: %d", ret);
                snprintf(debug_buff, sizeof(debug_buff), "[Z] micro_speech_process_audio error: %d\n", ret);
                rpmsg_send(&tty_ept, debug_buff, strlen(debug_buff));
            }
			g_eof_status = false; // Reset EOF status
        }

        // Signal processing completion for EOF synchronization
        k_sem_give(&ml_complete_sem);
    }
}

void setup() {
    LOG_INF("Starting micro_speech_openamp application");

	// Reset prediction counter
	count = 0;
    
    // Create the management thread
    // k_thread_create(&thread_mng_data, thread_mng_stack, APP_TASK_STACK_SIZE,
    //     rpmsg_mng_task,
    //     NULL, NULL, NULL, K_PRIO_COOP(8), 0, K_NO_WAIT);
	rpmsg_transport_start();
    
    // Create the data receiving thread: Highest priority to prevent data loss
    k_thread_create(&thread_receive_data, thread_receive_stack, APP_RECEIVE_TASK_STACK_SIZE,
        app_receive_data_thread,
        NULL, NULL, NULL, K_PRIO_COOP(5), 0, K_NO_WAIT);
    
    // Create the audio processing thread: Lower priority than receive thread
    k_thread_create(&thread_audio_processing_data, thread_audio_processing_stack, APP_AUDIO_PROCESSING_TASK_STACK_SIZE,
        app_audio_processing_thread,
        NULL, NULL, NULL, K_PRIO_COOP(6), 0, K_NO_WAIT);
}

void loop() {
    // Keep the main thread alive and let the other threads do their work
    k_sleep(K_MSEC(1000));
}
} /* extern "C" */