/*
 * Manage RPMSG communication for audio processing
 */

#ifndef RPMSG_TRANSPORT_H
#define RPMSG_TRANSPORT_H

#include <zephyr/kernel.h>
#include <openamp/open_amp.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure to hold a received RPMSG message.
 *
 * This is used in the message queue between the RPMSG callback and the
 * consumer thread.
 */
struct rpmsg_rcv_msg {
    void *data;
    size_t len;
};

/* --- Public kernel objects --- */

/**
 * @brief Message queue for incoming TTY messages.
 *
 * The RPMSG receive callback places messages here. The application's
 * data receiving thread consumes them.
 */
extern struct k_msgq tty_msgq;

/**
 * @brief RPMSG endpoint for TTY communication.
 *
 * The application thread creates and uses this endpoint.
 */
extern struct rpmsg_endpoint tty_ept;

/**
 * @brief Semaphore to signal that the RPMSG device is ready.
 *
 * The application thread waits on this semaphore before creating an endpoint.
 */
extern struct k_sem data_tty_ready_sem;

/**
 * @brief The main RPMSG device instance.
 *
 * Needed by the application to create an endpoint.
 */
extern struct rpmsg_device *rpdev;


/* --- Public functions --- */

/**
 * @brief Starts the RPMSG transport management task.
 *
 * This function initializes and starts a new thread to handle the
 * RPMSG platform initialization and management.
 */
void rpmsg_transport_start(void);

/**
 * @brief The callback function for receiving TTY data.
 *
 * This function is passed to rpmsg_create_ept.
 */
int rpmsg_recv_tty_callback(struct rpmsg_endpoint *ept, void *data,
                size_t len, uint32_t src, void *priv);

#ifdef __cplusplus
}
#endif

#endif /* RPMSG_TRANSPORT_H */