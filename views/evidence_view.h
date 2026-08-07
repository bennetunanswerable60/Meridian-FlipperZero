#pragma once

#include <gui/view.h>

#include "../helpers/mrd_gps.h"
#include "mrd_view_event.h"

typedef struct EvidenceView EvidenceView;

typedef void (*EvidenceViewCallback)(void* context, uint32_t event);

EvidenceView* evidence_view_alloc(void);
void evidence_view_free(EvidenceView* view);
View* evidence_view_get_view(EvidenceView* view);

void evidence_view_set_callback(EvidenceView* view, EvidenceViewCallback cb, void* context);
void evidence_view_update(EvidenceView* view, const MrdSnapshot* snap);

/** Which check the cursor is on, so the detail card knows what to open. */
uint8_t evidence_view_selected(EvidenceView* view);
