#pragma once

bool spray_init(unsigned int pump_gpio, unsigned int zone1_gpio, unsigned int zone2_gpio);
void spray_deinit();

// pest_pressure is typically avg insect count per successfully inferred image.
// Returns true when a spray action was triggered.
bool spray_apply_for_pressure(double pest_pressure);

// Apply spray action by cloud decision scope class:
// 0 = no spray, 1 = partial/medium spray, 2 = full/high spray.
bool spray_apply_for_scope(int scope_class);
