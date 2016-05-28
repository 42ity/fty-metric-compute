
# Agent-cm
Agent cm (computation) is SUBscribed on METRICS stream and computes
averages/min/max for defined time steps and PUBlish them back on a METRICS
stream.

## How to build

To build agent-cm project run:

```bash
./autogen.sh
./configure
make
make check # to run self-test
```

## Protocols

### METRIC stream

Agent is SUBscribed on METRICS stream (for now only for "realpower.default, 
"average.temperature", "average.humidity" topics) . Once the time step passed, it PUBlishes a 
computed value back as METRIC on the METRICS stream with the following properties:
  * subject is ```${original-subject}_${type}_${step}@${asset_name}```
  * there is ```x-cm-type``` field in aux stating the type
  * there is ```x-cm-step``` field in aux stating the step in [s]
  * there is ```x-cm-sum``` field in aux internal information
  * there is ```x-cm-count``` field in aux how many measurements where processed in that interval
  * there is ```time``` field in aux stating the *start* of computation (UNIXTIME in UTC)

### ASSETS stream

Agent is SUBscribed on ASSETS stream. Once it receives any "delete" or "retire" ASSET
message it will drop all computations for that asset.
### REQ register the subject and type/step to be computed
TBD

## State

Agent persists its state. In the "/var/lib/bios/bios-agent-cm/state.zpl"
