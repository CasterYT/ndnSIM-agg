import pandas as pd
import matplotlib.pyplot as plt
import os


def Consumer_rtt(file):
    """
    Draw a graph for consumer, x: Time y: RTT
    Data is captured when data packet returned

    :param file:
    :return:
    """
    try:
        # Load data from txt file
        print("Loading data from: ", file)
        data = pd.read_csv(file, sep="\s+", header=None)
        data.columns = ['Time', 'RTT']

        # Create a figure with a single axis
        fig, ax = plt.subplots()

        ax.set_xlabel('Time')
        ax.set_ylabel('RTT')
        ax.plot(data['Time'], data['RTT'], color='tab:red')
        ax.tick_params(axis='y', labelcolor='tab:red')

        plt.title("Consumer: RTT")
        plt.grid(True)
        plt.savefig("/home/dd/agg-ndnSIM/ns-3/src/ndnSIM/experiments/result/consumer_rtt.png")
        plt.close()

        return "Plot created and saved successfully."
    except FileNotFoundError as e:
        return f"File not found: {e}"
    except pd.errors.EmptyDataError as e:
        return f"One of the files is empty: {e}"
    except Exception as e:
        return f"An error occurred: {str(e)}"


def Consumer_window_rto(file1, file2):
    """
    Draw a graph for consumer, x: Time y: Window vs. RTO
    Data is captured every 5 ms

    :param file1:
    :param file2:
    :return:
    """
    try:
        # Load data from txt files
        print("Loading data from:", file1)
        data1 = pd.read_csv(file1, sep="\s+", header=None)
        data1.columns = ['Time', 'Window']

        print("Loading data from:", file2)
        data2 = pd.read_csv(file2, sep="\s+", header=None)
        data2.columns = ['Time', "RTO"]

        # Create a new figure
        fig, ax1 = plt.subplots()

        # First y-axis for RTO
        color = 'tab:red'
        ax1.set_xlabel('Time')
        ax1.set_ylabel('RTO', color=color)
        ax1.plot(data2['Time'], data2['RTO'], color=color)
        ax1.tick_params(axis='y', labelcolor=color)

        # Second y-axis for Window
        ax2 = ax1.twinx()
        color = 'tab:blue'
        ax2.set_ylabel("Window", color=color)
        ax2.plot(data1['Time'], data1['Window'], color=color)
        ax2.tick_params(axis='y', labelcolor=color)

        plt.title("Consumer: Window vs. RTO")
        plt.grid(True)
        plt.savefig("/home/dd/agg-ndnSIM/ns-3/src/ndnSIM/experiments/result/consumer_window_rto.png")
        plt.close()

        return "Plot created and saved successfully."
    except FileNotFoundError as e:
        return f"File not found: {e}"
    except pd.errors.EmptyDataError as e:
        return f"One of the files is empty: {e}"
    except Exception as e:
        return f"An error occurred: {str(e)}"



def main():
    # Print the current working directory
    print("Current Working Directory:", os.getcwd())

    # Print the list of files in the current directory
    print("Files in the current directory:", os.listdir())

    file1 = "/home/dd/agg-ndnSIM/ns-3/src/ndnSIM/examples/log/agg_consumer_window.txt"
    file2 = "/home/dd/agg-ndnSIM/ns-3/src/ndnSIM/examples/log/agg_consumer_RTO_periodical.txt"
    file3 = "/home/dd/agg-ndnSIM/ns-3/src/ndnSIM/examples/log/agg_consumer_RTT_packet.txt"

    # Check if files exist
    print("Checking if file1 exists:", os.path.exists(file1))
    print("Checking if file2 exists:", os.path.exists(file2))
    print("Checking if file3 exists:", os.path.exists(file3))

    # Return execution log
    result1 = Consumer_window_rto(file1, file2)
    print(result1)
    result2 = Consumer_rtt(file3)
    print(result2)


if __name__ == "__main__":
    main()
