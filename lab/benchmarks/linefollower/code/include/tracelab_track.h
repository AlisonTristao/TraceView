/* tracelab_track.h -- the line follower's track, for a plant written in C
 * that lives in the environment "Line-follower track" (linefollower.track).
 *
 * The plant must declare the states x, y, theta: its pose in the track's
 * frame (m, rad; drawn y down, the car pointing along (cos theta,
 * sin theta)). The environment places it on the track's start on every
 * reset, follows its progress, and gives the controller the preview of the
 * points ahead (sim.preview) -- the plant only queries the line.
 */
#ifndef TRACELAB_TRACK_H
#define TRACELAB_TRACK_H

#include <tracelab.h>

/* Distance (m) from the world point (x, y) to the centre of the line;
   INFINITY a couple of centimetres away from it. */
double tl_track_distance(double x, double y);

/* Half the line's width (m): a point is on the line within it. */
double tl_track_line_half_width(void);

#endif /* TRACELAB_TRACK_H */
