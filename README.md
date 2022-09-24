# fty-metric-compute
Agent fty-metric-compute read a few physics METRICS from the shared memory, computes
averages/min/max/consumption for defined time steps and publish them back on the shared memory.

## How to build

To build fty-metric-compute project run:

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=usr -DBUILD_TESTING=On ..
make
make check # to run self-test
sudo make install
```
## How to run

To run fty-metric-compute project:

* from within the source tree, run:

```bash
./src/fty-metric-compute
```

For the other options available, refer to the manual page of fty-metric-compute

* from an installed base, using systemd, run:

```bash
systemctl start fty-metric-compute
```

### Configuration file

Configuration file - fty-metric-compute.cfg - is currently ignored.

Agent reads environment variable BIOS\_LOG\_LEVEL to set verbosity level.

Agent persists its state in the /var/lib/fty/fty-metric-compute/state.zpl

## Architecture

### Overview

fty-metric-compute has 1 actor:

* fty-mc-server: main actor

It also has one built-in timer, which runs at the next configured 'step',
publishes computed metrics and saves the state.

## Protocols

### Published metrics

Agent doesn't publish any metrics.

### Published alerts

Agent doesn't publish any alerts.

### Mailbox requests

Agent doesn't receive any mailbox requests.

### Stream subscriptions

### ASSETS stream

Agent is SUBscribed on ASSETS stream. Once it receives any "delete" or "retire" ASSET
message it will drop all computations for that asset.

## Shared memory (metrics)

Agent publish metrics on the shared memory, for "realpower.default,
"average.temperature", "average.humidity", and more topics.

Once the time step passed, it writes
computed values as METRICs on the shared memory with the following properties:
  * subject is ```${original-subject}_${type}_${step}@${asset_name}```
  * there is ```x-cm-type``` field in aux stating the type
  * there is ```x-cm-step``` field in aux stating the step in [s]
  * there is ```x-cm-sum``` field in aux internal information
  * there is ```x-cm-count``` field in aux how many measurements where processed in that interval
  * there is ```time``` field in aux stating the *start* of computation (UNIXTIME in UTC)
