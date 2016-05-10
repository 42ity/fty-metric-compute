
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

Agent is SUBscribed on METRICS stream. Once the time step passed, it PUBlish
computed value back on the stream with following properties: 
  * subject is ```${original-subject}_${type}_${step}@device```
  * there is ```x-cm-type``` field in aux stating the type
  * there is ```x-cm-step``` field in aux stating the step
  * there is ```time``` field in aux stating the *start* of computation

### REQ register the subject and type/step to be computed
TBD
