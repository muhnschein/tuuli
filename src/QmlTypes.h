// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#pragma once

namespace Tuuli {

class Core;

// Registers the `harbour.tuuli 1.0` module: TabModel, HistoryModel, BookmarkModel,
// Settings and EngineMessages as singletons backed by the given Core. Safe to call
// again with another Core (tests); registration itself happens once per process.
void registerQmlTypes(Core *core);

} // namespace Tuuli
