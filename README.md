ndnSIM
======

[![Build Status](https://travis-ci.org/named-data-ndnSIM/ndnSIM.svg)](https://travis-ci.org/named-data-ndnSIM/ndnSIM)

A new release of [NS-3 based Named Data Networking (NDN) simulator](http://ndnsim.net/)
went through extensive refactoring and rewriting.  The key new features of the new
version:

- [NDN Packet Specification](http://named-data.net/doc/NDN-packet-spec/current/)

- ndnSIM uses implementation of basic NDN primitives from
  [ndn-cxx library (NDN C++ library with eXperimental eXtensions)](http://named-data.net/doc/ndn-cxx/)

  Based on version `0.7.0`

- All NDN forwarding and management is implemented directly using source code of
  [Named Data Networking Forwarding Daemon (NFD)](http://named-data.net/doc/NFD/)

  Based on version `0.7.0`

- Allows [simulation of real applications](http://ndnsim.net/guide-to-simulate-real-apps.html)
  written against ndn-cxx library

- Requires a modified version of NS-3 based on version `ns-3.30.1`

[ndnSIM documentation](http://ndnsim.net)
---------------------------------------------

For more information, including downloading and compilation instruction, please refer to
http://ndnsim.net or documentation in `docs/` folder.



How to configure this customized ndnSIM project 
=============
---

## Option 1: Clone for a new directory

- Clone from github repository
    ``` shell
    mkdir ndnSIM
    cd ndnSIM
    git clone https://github.com/named-data-ndnSIM/ns-3-dev.git ns-3
    git clone https://github.com/named-data-ndnSIM/pybindgen.git pybindgen
    git clone --recursive https://github.com/CasterYT/ndnSIM-complete.git ns-3/src/ndnSIM
    ```
- Build ndnSIM
    ``` shell
  cd ns-3
  ./waf configure -d debug
  ./waf
    ```
- Put scenario files `agg-aimd.cc` and `DataCenterTopology.txt` under `ns-3/scratch` directory
- Start simulation
    ```shell
  NS_LOG=ndn.Consumer:ndn.ConsumerINA:ndn.Aggregator:ndn.Producer ./waf --run agg-aimd
    ```


## Option 2: Run simulation for an existing ndnSIM project

- Assume you've cloned the official github repository using the following
    ```shell
    git clone https://github.com/named-data-ndnSIM/ns-3-dev.git ns-3
    git clone https://github.com/named-data-ndnSIM/pybindgen.git pybindgen
    git clone --recursive https://github.com/named-data-ndnSIM/ndnSIM.git ns-3/src/ndnSIM
    ```
- For this case, you need to update ndnSIM's source code first, assume you're in `ns-3` directory now
    ```shell
    cd src/ndnSIM
    git init
    git remote set-url origin https://github.com/CasterYT/ndnSIM-complete.git
  ```
  To check whether the remote link is set correctly
    ```shell
    git remote -v
    ```
    Update ndnSIM's source code
    ```shell
    git pull origin master
    ```

- Now your ndnSIM's source code should be up to date, then put scenario files `agg-aimd.cc` and `DataCenterTopology.txt` under `ns-3/scratch` directory
- Re-build the entire project, go back to `ns-3` directory first
    ```shell
    ./waf configure -d debug
    ./waf
    ```

- Start simulation
    ```shell
  NS_LOG=ndn.Consumer:ndn.ConsumerINA:ndn.Aggregator:ndn.Producer ./waf --run agg-aimd
    ```



