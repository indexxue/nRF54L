/**
 * @file event.c
 * @brief Event bitmask + wait (Zephyr k_sem / k_mutex).
 */

#include "event.h"

#include <string.h>

#include <zephyr/kernel.h>

typedef struct
{
    uint32_t mask;
    struct k_sem sem;
    struct k_mutex mutex;
    bool ready;
} event_self_t;

static event_self_t s_event;

void event_init(void)
{
    if (s_event.ready)
    {
        return;
    }

    (void)memset(&s_event, 0, sizeof(s_event));
    (void)k_sem_init(&s_event.sem, 0, 1);
    (void)k_mutex_init(&s_event.mutex);
    s_event.ready = true;
}

void event_set(event_id_e id)
{
    if (!s_event.ready)
    {
        return;
    }

    if (k_mutex_lock(&s_event.mutex, K_FOREVER) != 0)
    {
        return;
    }

    if ((s_event.mask & (uint32_t)id) != 0U)
    {
        (void)k_mutex_unlock(&s_event.mutex);
        /* Bit already set (queued work pending); still wake schedule waiters. */
        (void)k_sem_give(&s_event.sem);
        return;
    }

    s_event.mask |= (uint32_t)id;
    (void)k_mutex_unlock(&s_event.mutex);
    (void)k_sem_give(&s_event.sem);
}

void event_set_from_isr(event_id_e id)
{
    if (!s_event.ready)
    {
        return;
    }

    /* Keep ISR path lock-free for mask OR; wake waiter. */
    s_event.mask |= (uint32_t)id;
    (void)k_sem_give(&s_event.sem);
}

bool event_is_set(event_id_e id)
{
    bool status = false;

    if (!s_event.ready)
    {
        return false;
    }

    /* K_FOREVER: avoid false-negative when event_set holds the mutex briefly. */
    if (k_mutex_lock(&s_event.mutex, K_FOREVER) != 0)
    {
        return false;
    }

    status = ((s_event.mask & (uint32_t)id) != 0U);
    if (status)
    {
        s_event.mask &= ~(uint32_t)id;
    }

    (void)k_mutex_unlock(&s_event.mutex);
    return status;
}

void event_clear(event_id_e id)
{
    if (!s_event.ready)
    {
        return;
    }

    if (k_mutex_lock(&s_event.mutex, K_FOREVER) != 0)
    {
        return;
    }

    s_event.mask &= ~(uint32_t)id;
    (void)k_mutex_unlock(&s_event.mutex);
}

void event_schedule(void)
{
    if (!s_event.ready)
    {
        return;
    }

    (void)k_sem_take(&s_event.sem, K_FOREVER);
}
