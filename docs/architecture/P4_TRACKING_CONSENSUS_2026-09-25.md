# Camera tracking consensus and scene floor

The tripod solver rejected independent foreground movement with RANSAC, but its
public result subsequently marked every observed feature as solved. The point
overlay therefore reported rejected tracks as camera inliers. Rotation solves
now retain track identity through matching and report tracks that agree with the
estimated rotation in at least 80% of their evaluated frame pairs. Inlier count
and confidence use that actual consensus rather than a peak per-frame count.
Tripod RANSAC now checks cancellation inside its loop, matching the translating
camera path's cancellation behavior.

The moving-foreground regression also exposed incorrect camera model selection:
foreground motion could make pure rotation appear to have translation through
an otherwise degenerate essential matrix. The solver now compares accumulated
rotation against entire tracks, requiring majority background consensus. It
refines the rotation model's focal length beyond coarse search bins; otherwise
the FOV error accumulated at image edges and rejected valid background points.
Short adjacent-frame rotation fits alone cannot select the tripod model, since
they also approximate a slowly travelling camera.

Track presence now requires both coordinates to be finite. Malformed track row
lengths and zero-sized images are rejected before solver indexing/algebra.

The tracked scene floor previously used the normal of the winning random
three-point RANSAC sample, even when hundreds of noisy reconstructed points
supported the plane. It now refits the consensus with a double-precision
covariance eigensolve and updates membership, producing an orientation supported
by the full plane. Collinear/coincident geometry cannot establish a floor and
is rejected. The engine's existing scene-center fallback remains in place.

Regression coverage adds moving foreground points to the existing tripod solve,
checks floor orientation against a known noisy plane with outliers, and checks
malformed/cancelled inputs. This is shared Android/iOS C++ analysis; it does not
claim new planar object tracking, optical flow or motion blur features. Native
device footage validation and consolidated test execution remain separate.

Validated follow-up: host Tracking suite passed 9 tests / 62 checks in
engine/build/host/tracking-consensus-test.log. A foreground-contaminated tripod
now competes against a whole-track rotation model and refines FOV locally;
this prevents a false translating camera and preserves genuine background
inliers without relaxing the regression. Real-footage/device acceptance remains.
