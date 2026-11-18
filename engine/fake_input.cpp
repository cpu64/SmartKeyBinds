#include "./fake_input.hpp"
#include "./context.hpp"

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern Context ctx;

void create_device_description(int &virtual_input_device);
void enable_events(int &virtual_input_device);

void pass_event_on(input_event event)
{
    // write event to the file
    write(ctx.fake_input_device, &event, sizeof(input_event));
    // mark key state (up/down) in Gvar_.virtual_keyboard_state
    ctx.virtual_keyboard_state[event.code] = event.value;

    memset(&event, 0, sizeof(input_event));

    // now write synchronization event to the file
    write(ctx.fake_input_device, &event, sizeof(input_event));
}

int create_virtual_input_device()
{
    ////////////Initialize uinput device/////////
    int virtual_input_device = open("/dev/uinput", O_WRONLY | O_NONBLOCK); // open file descriptor

    if (virtual_input_device < 0) // error if opening the file was a failure
    {
        fprintf(
            stderr,
            "\n\n\n\n\n\n!!!!!!!error in open : %s >> Hint >> probably missing root premissions!!!!!!!!!\n\n\n\n\n\n",
            strerror(errno));
    }

    enable_events(
        virtual_input_device); // make uinput device capable of sending KEY, REL and SYN(Synchronization) events
    create_device_description(virtual_input_device); // create uinput device description

    // create the device via an IOCTL call
    if (ioctl(virtual_input_device, UI_DEV_CREATE))
    {
        fprintf(stderr, "Error in ioctl : UI_DEV_CREATE : %s \n", strerror(errno));
    }
    return virtual_input_device;
}

void create_device_description(int &virtual_input_device)
{
    ///////////uinput_user_dev struct for fake keyboard//////////
    struct uinput_user_dev fake_input_description;               // create keyboard description struct
    memset(&fake_input_description, 0, sizeof(uinput_user_dev)); // reset keyboard bits to 0
    snprintf(fake_input_description.name, UINPUT_MAX_NAME_SIZE,
             "Keyboard and mouse emulator");     // set keyboard name to "Virtual-Keyboard"
    fake_input_description.id.bustype = BUS_USB; // doesn't matter
    fake_input_description.id.vendor = 0x01;     // doesn't matter
    fake_input_description.id.product = 0x01;    // doesn't matter
    fake_input_description.id.version = 0x01;    // doesn't matter

    // now write the virtual keyboard description into uinput file descriptor
    if (write(virtual_input_device, &fake_input_description, sizeof(uinput_user_dev)) < 0)
    {
        fprintf(stderr, "Error in write(): uinput_user_dev struct into uinput file descriptor : %s \n",
                strerror(errno));
    }
}

void enable_events(int &virtual_input_device)
{
    /**configure the input device to send different types of events, informing subsystem which
     * type of input events we are using via ioctl calls.
     * UI_SET_EVBIT ioctl request is used on uinput descriptor to enable a type of event.
     **/
    // enable key press/release event
    if (ioctl(virtual_input_device, UI_SET_EVBIT, EV_KEY))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_EVBIT : EV_KEY : %s \n", strerror(errno));
    }
    ////////////////////////// enable keyboard keys here ////////////////////////// Esc=0 - 119=Pause/Break
    for (int key = 0; key < 500; key++)
    {
        if (ioctl(virtual_input_device, UI_SET_KEYBIT, key))
        {
            fprintf(stderr, "Error in ioctl : UI_SET_KEYBIT : key : %s \n", strerror(errno));
        }
    }
    ////////////////////////// enable mouse keys here //////////////////////////
    for (int key = BTN_LEFT; key < BTN_MIDDLE + 1; key++)
    {
        if (ioctl(virtual_input_device, UI_SET_KEYBIT, key))
        {
            fprintf(stderr, "Error in ioctl : UI_SET_KEYBIT : key : %s \n", strerror(errno));
        }
    }
    ////////////////////////// enable mouse movement and scroolwheel here //////////////////////////
    if (ioctl(virtual_input_device, UI_SET_EVBIT, EV_REL))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_EVBIT : EV_REL : %s \n", strerror(errno));
    }
    if (ioctl(virtual_input_device, UI_SET_RELBIT, REL_X))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_RELBIT : REL_X : %s \n", strerror(errno));
    }
    if (ioctl(virtual_input_device, UI_SET_RELBIT, REL_Y))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_RELBIT : REL_Y : %s \n", strerror(errno));
    }
    if (ioctl(virtual_input_device, UI_SET_RELBIT, REL_WHEEL))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_RELBIT : REL_WHEEL : %s \n", strerror(errno));
    }
    if (ioctl(virtual_input_device, UI_SET_RELBIT, REL_HWHEEL))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_RELBIT : REL_HWHEEL : %s \n", strerror(errno));
    }
    if (ioctl(virtual_input_device, UI_SET_RELBIT, REL_WHEEL_HI_RES))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_RELBIT : REL_WHEEL_HI_RES : %s \n", strerror(errno));
    }
    if (ioctl(virtual_input_device, UI_SET_RELBIT, REL_HWHEEL_HI_RES))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_RELBIT : REL_HWHEEL_HI_RES : %s \n", strerror(errno));
    }
    ////////////////////////// enable synchronization event //////////////////////////
    if (ioctl(virtual_input_device, UI_SET_EVBIT, EV_SYN))
    {
        fprintf(stderr, "Error in ioctl : UI_SET_EVBIT : EV_SYN : %s \n", strerror(errno));
    }
}

void key_press(int key, int state)
{
    input_event event = {0};
    event.type = 1;
    event.code = key;
    event.value = state;
    write(ctx.fake_input_device, &event, sizeof(input_event));
    memset(&event, 0, sizeof(input_event));
    write(ctx.fake_input_device, &event, sizeof(input_event));
    ctx.virtual_keyboard_state[key] = state;
}

void key_press(int key)
{
    input_event ev;

    // key down
    ev.type = EV_KEY;
    ev.code = key;
    ev.value = 1;
    write(ctx.fake_input_device, &ev, sizeof(ev));

    // key up
    ev.type = EV_KEY;
    ev.code = key;
    ev.value = 0;
    write(ctx.fake_input_device, &ev, sizeof(ev));

    // ONE sync for the whole action
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(ctx.fake_input_device, &ev, sizeof(ev));
}
