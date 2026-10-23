/*
 * File: feedback.h
 * Owner: Member 3
 * Status: Written for review; not yet built or tested on hardware.
 * Responsibility: Passive buzzer cues and station light.
 * Contract (docs/system-design.md): Feedback_StartCue(cue, now_ms),
 *                                   Feedback_Update(now_ms), Feedback_SetLight(on).
 *
 * Called by Actuator_Task ONLY, never from an ISR. No blocking and no RTOS timer:
 * StartCue starts the tone and records its end time. The actuator task must call
 * Feedback_Update(now_ms) every time it wakes, and use Feedback_GetNextDeadlineMs()
 * as one of its wake-up deadlines so the tone stops on time even with no new commands.
 */
#ifndef FEEDBACK_H
#define FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FEEDBACK_CUE_ACK,         /* short beep: button press acknowledged */
    FEEDBACK_CUE_FOOD_READY   /* longer beep: after dispensing         */
} FeedbackCue;

void Feedback_Init(void);
void Feedback_StartCue(FeedbackCue cue, uint32_t now_ms);   /* non-blocking */
void Feedback_Update(uint32_t now_ms);                      /* stops an expired cue */
bool Feedback_GetNextDeadlineMs(uint32_t *deadline_ms);     /* false if no cue is playing */

bool Feedback_SetLight(bool on);   /* returns the applied (commanded output) state */
bool Feedback_LightIsOn(void);

#endif /* FEEDBACK_H */