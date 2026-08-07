#include "../meridian_i.h"

void meridian_scene_about_on_enter(void* context) {
    MeridianApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Meridian " MERIDIAN_VERSION "\e#\n"
        "Is anything lying to you about\n"
        "where you are?\n"
        "\n"
        "\e#What it does\e#\n"
        "Civil GPS arrives below the "
        "noise floor, unencrypted and "
        "unsigned. Anything that "
        "transmits the same structure a "
        "few dB louder is believed. "
        "Meridian reads NMEA from a GPS "
        "module and watches eleven "
        "things a real sky does that a "
        "transmitter has to imitate.\n"
        "\n"
        "\e#The four paths\e#\n"
        "Position - teleports, frozen "
        "fixes, Doppler that disagrees "
        "with the track.\n"
        "Carrier - power that is too "
        "flat, too loud, or no longer "
        "rises with elevation.\n"
        "Geometry - a sky that stops "
        "moving, a DOP that never "
        "changes.\n"
        "Time - a receiver clock that "
        "drifts against the Flipper's "
        "own.\n"
        "\n"
        "\e#Controls\e#\n"
        "Left/Right page between "
        "Monitor, Sky, Drift and "
        "Evidence.\n"
        "Monitor: OK jumps to the "
        "evidence.\n"
        "Sky: OK switches between the "
        "sky plot and the carrier bars, "
        "Up/Down pick a satellite.\n"
        "Evidence: Up/Down choose a "
        "check, OK explains it.\n"
        "\n"
        "\e#Hardware\e#\n"
        "Any NMEA 0183 module on the "
        "GPIO header. Flipper TX to "
        "module RX, and back the other "
        "way. 9600 baud suits most "
        "modules; see Wiring.\n"
        "\n"
        "\e#What it cannot do\e#\n"
        "It cannot prove spoofing. One "
        "antenna sees statistical tells, "
        "not truth, and every check has "
        "an innocent explanation listed "
        "beside it. A single check never "
        "reaches the top verdict on its "
        "own, and the score never "
        "reaches 100.\n"
        "\n"
        "Jamming is reported separately. "
        "Denial and deception are "
        "different attacks and deserve "
        "different answers.\n"
        "\n"
        "\e#Receive only\e#\n"
        "There is no transmit path in "
        "this application. It cannot "
        "spoof, jam or interfere with "
        "anything.\n"
        "\n"
        "MIT licensed.\n"
        "github.com/at0m-b0mb/Meridian-FlipperZero\n"
        "by at0m-b0mb\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, MeridianViewText);
}

bool meridian_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void meridian_scene_about_on_exit(void* context) {
    MeridianApp* app = context;
    widget_reset(app->widget);
}
