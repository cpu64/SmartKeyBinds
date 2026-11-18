#pragma once

#include <linux/uinput.h>

#include <time.h>
#include <unistd.h>

#define UP 0
#define DOWN 1

inline void msleep(int mseconds)
{
    struct timespec request = {mseconds / 1000, (mseconds % 1000) * 1000000};
    nanosleep(&request, NULL);
}

int create_virtual_input_device();

void pass_event_on(input_event event);

void key_press(int key);

void key_press(int key, int state);

inline void key_hold(int key, int m_sec)
{
    key_press(key, DOWN);
    msleep(m_sec);
    key_press(key, UP);
}
