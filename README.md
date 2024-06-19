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
- Make sure you're under `ns-3` directory, remove `scratch` folder
  ```shell
  rm -rf scratch
  ```
- Clone the scenario files to `scratch`
  ```shell
  git clone https://github.com/CasterYT/ndnSIM-scenario.git scratch
  ```
- Start simulation
    ```shell
  NS_LOG=ndn.Consumer:ndn.ConsumerINA:ndn.Aggregator:ndn.Producer ./waf --run agg-aimd
    ```


## Option 2: Run simulation for an existing ndnSIM project

- Assume you've cloned the official github repository using the following, if not, please refer to https://ndnsim.net/current/getting-started.html
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
- Update ndnSIM's source code
    ```shell
    git pull origin master
    ```

- Now your ndnSIM's source code should be up to date, make sure you're under `ns-3` directory, remove `scratch` folder
  ```shell
  rm -rf scratch
  ```
- Clone the scenario files to `scratch`
  ```shell
  git clone https://github.com/CasterYT/ndnSIM-scenario.git scratch
  ```
- Re-build the entire project, go back to `ns-3` directory first
    ```shell
    ./waf configure -d debug
    ./waf
    ```

- Start simulation
    ```shell
  NS_LOG=ndn.Consumer:ndn.ConsumerINA:ndn.Aggregator:ndn.Producer ./waf --run agg-aimd
    ```



