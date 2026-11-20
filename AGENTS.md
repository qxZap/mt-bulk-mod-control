# AGENTS.md -- Mod Switch

Source of truth is `D:\MTLiveMap\mt_loader\plugins\mod_switch.cpp`; copy changes here by hand.

- The buttons are copies of the list's own button class (`KeepDisabledButton`'s), added to the panel it sits in
  (`Slot.Parent`). Their label is written into `ButtonName` raw before they are added, then into the name text block
  for 60 frames, because the button rewrites its own text for a while after it is built.
- Clicks are polled (`UButton.IsPressed` going up): a plugin cannot bind delegates.
- A row is pressed through its `ActionButton`'s inner `MenuButton` (`UButton.OnClicked` invocation list). The
  `MTButtonWidget.OnClicked` list itself is empty: the row listens through a native delegate. Firing that one pressed
  nothing (2026-11-20).
- State is read from the row's `NextLaunchStateTextBlock` ("disabled" / "enabled"). Rows are re-read after each pass
  (the list can rebuild), at most 6 passes, half a second apart.
- Confirmed in game 2026-11-20: 30 rows pressed each way.
