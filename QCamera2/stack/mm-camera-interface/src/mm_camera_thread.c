/* Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cam_semaphore.h>

#include "mm_camera_dbg.h"
#include "mm_camera_interface.h"
#include "mm_camera.h"

typedef enum {
    /* poll entries updated */
    MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED,
    /* poll entries updated asynchronous */
    MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED_ASYNC,
    /* commit updates */
    MM_CAMERA_PIPE_CMD_COMMIT,
    /* exit */
    MM_CAMERA_PIPE_CMD_EXIT,
    /* max count */
    MM_CAMERA_PIPE_CMD_MAX
} mm_camera_pipe_cmd_type_t;

typedef enum {
    MM_CAMERA_POLL_TASK_STATE_STOPPED,
    MM_CAMERA_POLL_TASK_STATE_POLL,     /* polling pid in polling state. */
    MM_CAMERA_POLL_TASK_STATE_MAX
} mm_camera_poll_task_state_type_t;

typedef struct {
    uint32_t cmd;
    mm_camera_event_t event;
} mm_camera_sig_evt_t;


/*===========================================================================
 * FUNCTION   : mm_camera_poll_sig_async
 *
 * DESCRIPTION: Asynchoronous call to send a command through pipe.
 *
 * PARAMETERS :
 *   @poll_cb      : ptr to poll thread object
 *   @cmd          : command to be sent
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
static int32_t mm_camera_poll_sig_async(mm_camera_poll_thread_t *poll_cb,
                                  uint32_t cmd)
{
    /* send through pipe */
    /* get the mutex */
    mm_camera_sig_evt_t cmd_evt;

    LOGD("E cmd = %d",cmd);
    memset(&cmd_evt, 0, sizeof(cmd_evt));
    cmd_evt.cmd = cmd;
    pthread_mutex_lock(&poll_cb->mutex);
    /* reset the statue to false */
    poll_cb->status = FALSE;

    /* send cmd to worker */
    ssize_t len = write(poll_cb->pfds[1], &cmd_evt, sizeof(cmd_evt));
    if (len < 1) {
        LOGW("len = %lld, errno = %d",
                (long long int)len, errno);
        /* Avoid waiting for the signal */
        pthread_mutex_unlock(&poll_cb->mutex);
        return 0;
    }
    LOGD("begin IN mutex write done, len = %lld",
            (long long int)len);
    pthread_mutex_unlock(&poll_cb->mutex);
    LOGD("X");
    return 0;
}




/*===========================================================================
 * FUNCTION   : mm_camera_poll_sig
 *
 * DESCRIPTION: synchorinzed call to send a command through pipe.
 *
 * PARAMETERS :
 *   @poll_cb      : ptr to poll thread object
 *   @cmd          : command to be sent
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
static int32_t mm_camera_poll_sig(mm_camera_poll_thread_t *poll_cb,
                                  uint32_t cmd)
{
    /* send through pipe */
    /* get the mutex */
    mm_camera_sig_evt_t cmd_evt;

    LOGD("E cmd = %d",cmd);
    memset(&cmd_evt, 0, sizeof(cmd_evt));
    cmd_evt.cmd = cmd;
    pthread_mutex_lock(&poll_cb->mutex);
    /* reset the statue to false */
    poll_cb->status = FALSE;
    /* send cmd to worker */

    ssize_t len = write(poll_cb->pfds[1], &cmd_evt, sizeof(cmd_evt));
    if(len < 1) {
        LOGW("len = %lld, errno = %d",
                (long long int)len, errno);
        /* Avoid waiting for the signal */
        pthread_mutex_unlock(&poll_cb->mutex);
        return 0;
    }
    LOGD("begin IN mutex write done, len = %lld",
            (long long int)len);
    /* wait till worker task gives positive signal */
    if (FALSE == poll_cb->status) {
        LOGD("wait");
        pthread_cond_wait(&poll_cb->cond_v, &poll_cb->mutex);
    }
    /* done */
    pthread_mutex_unlock(&poll_cb->mutex);
    LOGD("X");
    return 0;
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_sig
 *
 * DESCRIPTION: signal the status of done
 *
 * PARAMETERS :
 *   @poll_cb : ptr to poll thread object
 *
 * RETURN     : none
 *==========================================================================*/
static void mm_camera_poll_sig_done(mm_camera_poll_thread_t *poll_cb)
{
    pthread_mutex_lock(&poll_cb->mutex);
    poll_cb->status = TRUE;
    pthread_cond_signal(&poll_cb->cond_v);
    LOGD("done, in mutex");
    pthread_mutex_unlock(&poll_cb->mutex);
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_set_state
 *
 * DESCRIPTION: set a polling state
 *
 * PARAMETERS :
 *   @poll_cb : ptr to poll thread object
 *   @state   : polling state (stopped/polling)
 *
 * RETURN     : none
 *==========================================================================*/
static void mm_camera_poll_set_state(mm_camera_poll_thread_t *poll_cb,
                                     mm_camera_poll_task_state_type_t state)
{
    poll_cb->state = state;
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_proc_pipe
 *
 * DESCRIPTION: polling thread routine to process pipe
 *
 * PARAMETERS :
 *   @poll_cb : ptr to poll thread object
 *
 * RETURN     : none
 *==========================================================================*/
static void mm_camera_poll_proc_pipe(mm_camera_poll_thread_t *poll_cb)
{
    ssize_t read_len;
    int i;
    mm_camera_sig_evt_t cmd_evt;
    read_len = read(poll_cb->pfds[0], &cmd_evt, sizeof(cmd_evt));
    LOGD("read_fd = %d, read_len = %d, expect_len = %d cmd = %d",
          poll_cb->pfds[0], (int)read_len, (int)sizeof(cmd_evt), cmd_evt.cmd);
    switch (cmd_evt.cmd) {
    case MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED:
    case MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED_ASYNC:
        /* we always have index 0 for pipe read */
        poll_cb->num_fds = 0;
        poll_cb->poll_fds[poll_cb->num_fds].fd = poll_cb->pfds[0];
        poll_cb->poll_fds[poll_cb->num_fds].events = POLLIN|POLLRDNORM|POLLPRI;
        poll_cb->num_fds++;

        if (MM_CAMERA_POLL_TYPE_EVT == poll_cb->poll_type &&
                poll_cb->num_fds < MAX_STREAM_NUM_IN_BUNDLE) {
            if (poll_cb->poll_entries[0].fd >= 0) {
                /* fd is valid, we update poll_fds */
                poll_cb->poll_fds[poll_cb->num_fds].fd = poll_cb->poll_entries[0].fd;
                poll_cb->poll_fds[poll_cb->num_fds].events = POLLIN|POLLRDNORM|POLLPRI;
                poll_cb->num_fds++;
            }
        } else if (MM_CAMERA_POLL_TYPE_DATA == poll_cb->poll_type &&
                poll_cb->num_fds <= MAX_STREAM_NUM_IN_BUNDLE) {
            for(i = 0; i < MAX_STREAM_NUM_IN_BUNDLE; i++) {
                if(poll_cb->poll_entries[i].fd >= 0) {
                    /* fd is valid, we update poll_fds to this fd */
                    poll_cb->poll_fds[poll_cb->num_fds].fd = poll_cb->poll_entries[i].fd;
                    poll_cb->poll_fds[poll_cb->num_fds].events = POLLIN|POLLRDNORM|POLLPRI;
                    poll_cb->num_fds++;
                } else {
                    /* fd is invalid, we set the entry to -1 to prevent polling.
                     * According to spec, polling will not poll on entry with fd=-1.
                     * If this is not the case, we need to skip these invalid fds
                     * when updating this array.
                     * We still keep fd=-1 in this array because this makes easier to
                     * map cb associated with this fd once incoming data avail by directly
                     * using the index-1(0 is reserved for pipe read, so need to reduce index by 1) */
                    poll_cb->poll_fds[poll_cb->num_fds].fd = -1;
                    poll_cb->poll_fds[poll_cb->num_fds].events = 0;
                    poll_cb->num_fds++;
                }
            }
        }
        if (cmd_evt.cmd != MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED_ASYNC)
            mm_camera_poll_sig_done(poll_cb);
        break;

    case MM_CAMERA_PIPE_CMD_COMMIT:
        mm_camera_poll_sig_done(poll_cb);
        break;
    case MM_CAMERA_PIPE_CMD_EXIT:
    default:
        mm_camera_poll_set_state(poll_cb, MM_CAMERA_POLL_TASK_STATE_STOPPED);
        mm_camera_poll_sig_done(poll_cb);
        break;
    }
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_fn
 *
 * DESCRIPTION: polling thread routine
 *
 * PARAMETERS :
 *   @poll_cb : ptr to poll thread object
 *
 * RETURN     : none
 *==========================================================================*/
static void *mm_camera_poll_fn(mm_camera_poll_thread_t *poll_cb)
{
    int rc = 0, i;

    if (NULL == poll_cb) {
        LOGE("poll_cb is NULL!\n");
        return NULL;
    }
    LOGD("poll type = %d, num_fd = %d poll_cb = %p\n",
          poll_cb->poll_type, poll_cb->num_fds,poll_cb);
    do {
         for(i = 0; i < poll_cb->num_fds; i++) {
            poll_cb->poll_fds[i].events = POLLIN|POLLRDNORM|POLLPRI;
         }

         rc = poll(poll_cb->poll_fds, poll_cb->num_fds, poll_cb->timeoutms);
         if(rc > 0) {
            if ((poll_cb->poll_fds[0].revents & POLLIN) &&
                (poll_cb->poll_fds[0].revents & POLLRDNORM)) {
                /* if we have data on pipe, we only process pipe in this iteration */
                LOGD("cmd received on pipe\n");
                mm_camera_poll_proc_pipe(poll_cb);
            } else {
                for(i=1; i<poll_cb->num_fds; i++) {
                    /* Checking for ctrl events */
                    if ((poll_cb->poll_type == MM_CAMERA_POLL_TYPE_EVT) &&
                        (poll_cb->poll_fds[i].revents & POLLPRI)) {
                        LOGD("mm_camera_evt_notify\n");
                        if (NULL != poll_cb->poll_entries[i-1].notify_cb) {
                            poll_cb->poll_entries[i-1].notify_cb(poll_cb->poll_entries[i-1].user_data);
                        }
                    }

                    if ((MM_CAMERA_POLL_TYPE_DATA == poll_cb->poll_type) &&
                        (poll_cb->poll_fds[i].revents & POLLIN) &&
                        (poll_cb->poll_fds[i].revents & POLLRDNORM)) {
                        LOGD("mm_stream_data_notify\n");
                        if (NULL != poll_cb->poll_entries[i-1].notify_cb) {
                            poll_cb->poll_entries[i-1].notify_cb(poll_cb->poll_entries[i-1].user_data);
                        }
                    }
                }
            }
        } else {
            /* in error case sleep 10 us and then continue. hard coded here */
            usleep(10);
            continue;
        }
    } while ((poll_cb != NULL) && (poll_cb->state == MM_CAMERA_POLL_TASK_STATE_POLL));
    return NULL;
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_thread
 *
 * DESCRIPTION: polling thread entry function
 *
 * PARAMETERS :
 *   @data    : ptr to poll thread object
 *
 * RETURN     : none
 *==========================================================================*/
static void *mm_camera_poll_thread(void *data)
{
    mm_camera_poll_thread_t *poll_cb = (mm_camera_poll_thread_t *)data;

    mm_camera_cmd_thread_name(poll_cb->threadName);
    /* add pipe read fd into poll first */
    poll_cb->poll_fds[poll_cb->num_fds++].fd = poll_cb->pfds[0];

    mm_camera_poll_sig_done(poll_cb);
    mm_camera_poll_set_state(poll_cb, MM_CAMERA_POLL_TASK_STATE_POLL);
    return mm_camera_poll_fn(poll_cb);
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_thread
 *
 * DESCRIPTION: notify the polling thread that entries for polling fd have
 *              been updated
 *
 * PARAMETERS :
 *   @poll_cb : ptr to poll thread object
 *
 * RETURN     : none
 *==========================================================================*/
int32_t mm_camera_poll_thread_notify_entries_updated(mm_camera_poll_thread_t * poll_cb)
{
    /* send poll entries updated signal to poll thread */
    return mm_camera_poll_sig(poll_cb, MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED);
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_thread_commit_updates
 *
 * DESCRIPTION: sync with all previously pending async updates
 *
 * PARAMETERS :
 *   @poll_cb : ptr to poll thread object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_poll_thread_commit_updates(mm_camera_poll_thread_t * poll_cb)
{
    return mm_camera_poll_sig(poll_cb, MM_CAMERA_PIPE_CMD_COMMIT);
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_thread_add_poll_fd
 *
 * DESCRIPTION: add a new fd into polling thread
 *
 * PARAMETERS :
 *   @poll_cb   : ptr to poll thread object
 *   @idx       : Object index.
 *   @handler   : stream handle if channel data polling thread,
 *                0 if event polling thread
 *   @fd        : file descriptor need to be added into polling thread
 *   @notify_cb : callback function to handle if any notify from fd
 *   @userdata  : user data ptr
 *   @call_type : Whether its Synchronous or Asynchronous call
 *
 * RETURN     : none
 *==========================================================================*/
int32_t mm_camera_poll_thread_add_poll_fd(mm_camera_poll_thread_t * poll_cb,
        uint8_t idx, uint32_t handler, int32_t fd, mm_camera_poll_notify_t notify_cb,
        void* userdata, mm_camera_call_type_t call_type)
{
    int32_t rc = -1;

    if (MAX_STREAM_NUM_IN_BUNDLE > idx) {
        poll_cb->poll_entries[idx].fd = fd;
        poll_cb->poll_entries[idx].handler = handler;
        poll_cb->poll_entries[idx].notify_cb = notify_cb;
        poll_cb->poll_entries[idx].user_data = userdata;
        /* send poll entries updated signal to poll thread */
        if (call_type == mm_camera_sync_call ) {
            rc = mm_camera_poll_sig(poll_cb, MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED);
        } else {
            rc = mm_camera_poll_sig_async(poll_cb, MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED_ASYNC );
        }
    } else {
        LOGE("invalid handler %d (%d)", handler, idx);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_camera_poll_thread_del_poll_fd
 *
 * DESCRIPTION: delete a fd from polling thread
 *
 * PARAMETERS :
 *   @poll_cb   : ptr to poll thread object
 *   @idx       : Object index.
 *   @handler   : stream handle if channel data polling thread,
 *                0 if event polling thread
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_camera_poll_thread_del_poll_fd(mm_camera_poll_thread_t * poll_cb,
        uint8_t idx, uint32_t handler, mm_camera_call_type_t call_type)
{
    int32_t rc = -1;

    if ((MAX_STREAM_NUM_IN_BUNDLE > idx) &&
        (handler == poll_cb->poll_entries[idx].handler)) {
        /* reset poll entry */
        poll_cb->poll_entries[idx].fd = -1; /* set fd to invalid */
        poll_cb->poll_entries[idx].handler = 0;
        poll_cb->poll_entries[idx].notify_cb = NULL;

        /* send poll entries updated signal to poll thread */
        if (call_type == mm_camera_sync_call ) {
            rc = mm_camera_poll_sig(poll_cb, MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED);
        } else {
            rc = mm_camera_poll_sig_async(poll_cb, MM_CAMERA_PIPE_CMD_POLL_ENTRIES_UPDATED_ASYNC );
        }
    } else {
        if ((MAX_STREAM_NUM_IN_BUNDLE <= idx) ||
                (poll_cb->poll_entries[idx].handler != 0)) {
            LOGE("invalid handler %d (%d)", poll_cb->poll_entries[idx].handler,
                    idx);
            rc = -1;
        } else {
            LOGW("invalid handler %d (%d)", handler, idx);
            rc = 0;
        }
    }

    return rc;
}

int32_t mm_camera_poll_thread_launch(mm_camera_poll_thread_t * poll_cb,
                                     mm_camera_poll_thread_type_t poll_type)
{
    int32_t rc = 0;
    size_t i = 0, cnt = 0;
    poll_cb->poll_type = poll_type;
    pthread_condattr_t cond_attr;

    //Initialize poll_fds
    cnt = sizeof(poll_cb->poll_fds) / sizeof(poll_cb->poll_fds[0]);
    for (i = 0; i < cnt; i++) {
        poll_cb->poll_fds[i].fd = -1;
    }
    //Initialize poll_entries
    cnt = sizeof(poll_cb->poll_entries) / sizeof(poll_cb->poll_entries[0]);
    for (i = 0; i < cnt; i++) {
        poll_cb->poll_entries[i].fd = -1;
    }
    //Initialize pipe fds
    poll_cb->pfds[0] = -1;
    poll_cb->pfds[1] = -1;
    rc = pipe(poll_cb->pfds);
    if(rc < 0) {
        LOGE("pipe open rc=%d\n", rc);
        return -1;
    }

    poll_cb->timeoutms = -1;  /* Infinite seconds */

    LOGD("poll_type = %d, read fd = %d, write fd = %d timeout = %d",
         poll_cb->poll_type,
        poll_cb->pfds[0], poll_cb->pfds[1],poll_cb->timeoutms);

    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);

    pthread_mutex_init(&poll_cb->mutex, NULL);
    pthread_cond_init(&poll_cb->cond_v, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    /* launch the thread */
    pthread_mutex_lock(&poll_cb->mutex);
    poll_cb->status = 0;
    pthread_create(&poll_cb->pid, NULL, mm_camera_poll_thread, (void *)poll_cb);
    if(!poll_cb->status) {
        pthread_cond_wait(&poll_cb->cond_v, &poll_cb->mutex);
    }

    pthread_mutex_unlock(&poll_cb->mutex);
    LOGD("End");
    return rc;
}

int32_t mm_camera_poll_thread_release(mm_camera_poll_thread_t *poll_cb)
{
    int32_t rc = 0;
    if(MM_CAMERA_POLL_TASK_STATE_STOPPED == poll_cb->state) {
        LOGE("err, poll thread is not running.\n");
        return rc;
    }

    /* send exit signal to poll thread */
    mm_camera_poll_sig(poll_cb, MM_CAMERA_PIPE_CMD_EXIT);
    /* wait until poll thread exits */
    if (pthread_join(poll_cb->pid, NULL) != 0) {
        LOGD("pthread dead already\n");
    }

    /* close pipe */
    if(poll_cb->pfds[0] >= 0) {
        close(poll_cb->pfds[0]);
    }
    if(poll_cb->pfds[1] >= 0) {
        close(poll_cb->pfds[1]);
    }

    pthread_mutex_destroy(&poll_cb->mutex);
    pthread_cond_destroy(&poll_cb->cond_v);
    memset(poll_cb, 0, sizeof(mm_camera_poll_thread_t));
    poll_cb->pfds[0] = -1;
    poll_cb->pfds[1] = -1;
    return rc;
}

static void *mm_camera_cmd_thread(void *data)
{
    int running = 1;
    int ret;
    mm_camera_cmd_thread_t *cmd_thread =
                (mm_camera_cmd_thread_t *)data;
    mm_camera_cmdcb_t* node = NULL;

    mm_camera_cmd_thread_name(cmd_thread->threadName);
    do {
        do {
            ret = cam_sem_wait(&cmd_thread->cmd_sem);
            if (ret != 0 && errno != EINVAL) {
                LOGE("cam_sem_wait error (%s)",
                            strerror(errno));
                return NULL;
            }
        } while (ret != 0);

        /* we got notified about new cmd avail in cmd queue */
        node = (mm_camera_cmdcb_t*)cam_queue_deq(&cmd_thread->cmd_queue);
        while (node != NULL) {
            switch (node->cmd_type) {
            case MM_CAMERA_CMD_TYPE_EVT_CB:
            case MM_CAMERA_CMD_TYPE_DATA_CB:
            case MM_CAMERA_CMD_TYPE_REQ_DATA_CB:
            case MM_CAMERA_CMD_TYPE_SUPER_BUF_DATA_CB:
            case MM_CAMERA_CMD_TYPE_CONFIG_NOTIFY:
            case MM_CAMERA_CMD_TYPE_START_ZSL:
            case MM_CAMERA_CMD_TYPE_STOP_ZSL:
            case MM_CAMERA_CMD_TYPE_GENERAL:
            case MM_CAMERA_CMD_TYPE_FLUSH_QUEUE:
                if (NULL != cmd_thread->cb) {
                    cmd_thread->cb(node, cmd_thread->user_data);
                }
                break;
            case MM_CAMERA_CMD_TYPE_EXIT:
            default:
                running = 0;
                break;
            }
            free(node);
            node = (mm_camera_cmdcb_t*)cam_queue_deq(&cmd_thread->cmd_queue);
        } /* (node != NULL) */
    } while (running);
    return NULL;
}

int32_t mm_camera_cmd_thread_launch(mm_camera_cmd_thread_t * cmd_thread,
                                    mm_camera_cmd_cb_t cb,
                                    void* user_data)
{
    int32_t rc = 0;

    cam_sem_init(&cmd_thread->cmd_sem, 0);
    cam_sem_init(&cmd_thread->sync_sem, 0);
    cam_queue_init(&cmd_thread->cmd_queue);
    cmd_thread->cb = cb;
    cmd_thread->user_data = user_data;
    cmd_thread->is_active = TRUE;

    /* launch the thread */
    pthread_create(&cmd_thread->cmd_pid,
                   NULL,
                   mm_camera_cmd_thread,
                   (void *)cmd_thread);
    return rc;
}

int32_t mm_camera_cmd_thread_name(const char* name)
{
    int32_t rc = 0;
    /* name the thread */
    if (name && strlen(name))
        prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
    return rc;
}


int32_t mm_camera_cmd_thread_stop(mm_camera_cmd_thread_t * cmd_thread)
{
    int32_t rc = 0;
    mm_camera_cmdcb_t* node = (mm_camera_cmdcb_t *)malloc(sizeof(mm_camera_cmdcb_t));
    if (NULL == node) {
        LOGE("No memory for mm_camera_cmdcb_t");
        return -1;
    }

    memset(node, 0, sizeof(mm_camera_cmdcb_t));
    node->cmd_type = MM_CAMERA_CMD_TYPE_EXIT;

    cam_queue_enq(&cmd_thread->cmd_queue, node);
    cam_sem_post(&cmd_thread->cmd_sem);

    /* wait until cmd thread exits */
    if (pthread_join(cmd_thread->cmd_pid, NULL) != 0) {
        LOGD("pthread dead already\n");
    }
    return rc;
}

int32_t mm_camera_cmd_thread_destroy(mm_camera_cmd_thread_t * cmd_thread)
{
    int32_t rc = 0;
    cam_queue_deinit(&cmd_thread->cmd_queue);
    cam_sem_destroy(&cmd_thread->cmd_sem);
    cam_sem_destroy(&cmd_thread->sync_sem);
    memset(cmd_thread, 0, sizeof(mm_camera_cmd_thread_t));
    return rc;
}

int32_t mm_camera_cmd_thread_release(mm_camera_cmd_thread_t * cmd_thread)
{
    int32_t rc = 0;
    rc = mm_camera_cmd_thread_stop(cmd_thread);
    if (0 == rc) {
        rc = mm_camera_cmd_thread_destroy(cmd_thread);
    }
    return rc;
}
