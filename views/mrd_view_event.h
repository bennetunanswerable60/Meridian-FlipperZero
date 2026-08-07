#pragma once

/*
 * What a view is allowed to tell its scene.
 *
 * Views never navigate. They report that something happened and the scene
 * decides what that means, which is what keeps the live screens swappable:
 * every one of them emits PageNext and PagePrev, and the monitor scene turns
 * those into whichever view comes next in the cycle.
 */
/** Live screens in the left/right cycle. Declared here rather than in the app
 * header so a view can draw the page indicator without depending on the app. */
#define MRD_PAGE_COUNT 4

typedef enum {
    MrdViewEventPageNext = 0,
    MrdViewEventPagePrev,
    /** Open the detail card for whatever the view currently has selected. */
    MrdViewEventDetail,
    /** A timed view has finished playing. */
    MrdViewEventDone,
} MrdViewEvent;
