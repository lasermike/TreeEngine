---
issue: 33
---
# User controls

## Goal
Upgrade the user controls to be more accessible and comfortable to someone who plays minecraft by adding a walk around camera in addition to the fly camera.    

## What it should do
- add mouse look that is on by default and an ESC toggles it with the menu selection cursor that is the default today.
- change the movement direction to match minecraft's WASD movement.
- change speed to be time based rather than fixed step per tick.
- add toggle between fly and walk camera but double tapping the space bar. use the fly camera whenever camera animation is specified. 
- fly camera change vertical position with space for up and ctrl for down.  in fly camera shift provides speed boost.  fly camera should have a speed limit and acceleration/deceleration when moving.  mouse scroll wheel changes maximum speed boost. fly camera ignores terrain height and can move through geometry.
- walk camera needs a sense of gravity and maintain a certain height above the terrain, and a minimum height is enforced.  check the height map each frame to determine the height of the camera, or use height = 0 when no terrain is present.  walk camera uses space to jump and shift to crouch.  walk camera should not be able to fly up or down.  walk camera should have a gravity effect and a collision effect with the ground. in walk mode Ctrl sprints.  walk camera should have a speed limit and acceleration/deceleration.  
- this change affects movement in every scene, but i do not expect gold image or other test changes unless a test is specifically testing the camera movement.  The walk camera should be the default camera when starting a new scene.
- keep the older keys for camera orientation.
- a camera placed by a capture file (render tests, performance runs) counts as animated: it uses the fly camera, so
  gold images and perf runs don't change.

## Defaults (Minecraft-like; tune in the PR if they feel wrong)
| | |
|---|---|
| Eye height | 1.6 m (crouched 1.3 m) |
| Jump height | 1.25 m |
| Walk speed | 4.3 m/s; Ctrl sprint ×1.3; crouch ×0.3 |
| Fly speed | about 11 m/s; Shift boost ×3 by default, scroll wheel sets it from ×1 to ×10 |
| Double-tap window | 0.3 s between Space presses |

## Plan (two PRs, both "Part of #33"; the second closes it)
1. Walk and fly cameras: time-based speed with acceleration, Minecraft-style level WASD movement, double-tap Space
   toggle, gravity, jump, crouch and Ctrl sprint on the terrain height (or 0 without terrain), fly for animated and
   captured cameras.
2. Mouse look (on by default, Esc releases the cursor for the menus), scroll-wheel boost.

## Out of scope
- a full collsion and physics system.

## References
- none

## Open questions
- none