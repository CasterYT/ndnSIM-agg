#!/bin/bash

# Start simulation
NS_LOG=ndn.Consumer:ndn.ConsumerINA:ndn.Aggregator ./waf --run agg-aimd-test

# Generate simulation result
python3 ResultMeasurement.py