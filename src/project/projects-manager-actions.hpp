#pragma once
// The open projects, as actions (ProjectsManager changes only through these): logged, never undone.
//
//   project.new        a new blank one, active. made: its id
//   project.close      one (the active one if none is given; the last one: none left). An unsaved one is refused unless
//                      discard is on: whoever drives it asks first (the interface: file.close)
//   project.activate   another one active
//   project.save       one (the active one if none is given) to its file, or to `path` (then that's
//                      its file). Refused, with the error, if it can't be written
//   project.open       a .komal file, active (one already open: that one, active). made: its id.
//                      Refused, with the error, if it can't be read
//
// None of them while the active one has an edit in progress (a stroke, a drag): it ends first.

#include "command/actions.hpp"
#include "project/project.hpp"   // ProjectArgs (kPath) too

class ProjectsManager;

namespace ProjectArgs {
inline constexpr ArgKey<ProjectId> kProject{ "project" };
inline constexpr ArgKey<bool>       kDiscard{ "discard" };   // project.close: its unsaved changes too
}

void registerProjectsManagerActions(ActionRegistry& actions, ProjectsManager& projects);
