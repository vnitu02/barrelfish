%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% Copyright (c) 2016, ETH Zurich.
% All rights reserved.
%
% This file is distributed under the terms in the attached LICENSE file.
% If you do not find this file, copies can be found by writing to:
% ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Statically-initialised hardware facts for the Fixed Virtual Platform A15x4,
% or qemu.

cpu_driver(cortexA15, "/armv7/sbin/cpu_a15ve").
monitor(cortexA15, "/armv7/sbin/monitor").

% One cluster of two Cortex A15s
arm_core(16'000000,cortexA15).
arm_core(16'000001,cortexA15).

kernel_timer_irq(29).
