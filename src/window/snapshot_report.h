#pragma once

#include "window/window_classifier.h"
#include "window/window_snapshot.h"

#include <iosfwd>
#include <span>

namespace stage_manager::window {

void write_snapshot_report(std::ostream& output,
                           const WindowSnapshotBatch& batch,
                           std::span<const ClassificationResult> classifications);

} // namespace stage_manager::window
