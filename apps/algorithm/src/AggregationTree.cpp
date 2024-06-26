#include "../include/AggregationTree.hpp"
#include "../utility/utility.hpp"

#include <iostream>
#include <cmath> // For ceil
#include <numeric> // For std::accumulate
#include <cassert> // For assert
#include <fstream>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <limits>
#include <unordered_map>
#include <climits>
#include <stdexcept>
#include <algorithm>




AggregationTree::AggregationTree(std::string file){
    filename = file;
    fullList = Utility::getContextInfo(filename);
    CHList = fullList;
    linkCostMatrix = Utility::GetAllLinkCost(filename);
    //graph = Utility::initializeGraph(filename);
    //std::cout << "Finish initialization!" << std::endl;
}


std::string AggregationTree::findCH(std::vector<std::string> clusterNodes, std::vector<std::string> clusterHeadCandidate, std::string client) {

    std::string CH = client;
    // Initiate a large enough cost
    int leastCost = 1000;

    for (const auto& headCandidate : clusterHeadCandidate) {
        bool canBeCH = true;

        for (const auto& node : clusterNodes) {
            if (linkCostMatrix[node][client] < linkCostMatrix[node][headCandidate]) {
                canBeCH = false;
                break;
            }
        }

        // This candidate is closer to client
        long long totalCost = 0;
        if (canBeCH) {
            for (const auto& node : clusterNodes) {
                totalCost += linkCostMatrix[node][headCandidate];
            }
            int averageCost = static_cast<int>(totalCost / clusterNodes.size());

            if (averageCost < leastCost) {
                leastCost = averageCost;
                CH = headCandidate;
            }
        }
    }

    if (CH == client) {
        std::cerr << "No CH is found for current cluster!!!!!!!!!!!" << std::endl;
        return CH;
    } else {
        std::cout << "CH " << CH << " is chosen." << std::endl;
        return CH;
    }


}

// End of cluster head construction

bool AggregationTree::aggregationTreeConstruction(std::vector<std::string> dataPointNames, int C) {

    // Compute N
    int N = dataPointNames.size();

    // Compute the number of clusters k
    int numClusters = static_cast<int>(ceil(static_cast<double>(N) / C));

    // Create a vector to store cluster assignments
    std::vector<int> clusterAssignment(N);
    for (int i = 0; i < N; ++i) {
        clusterAssignment[i] = i % numClusters; // Cluster assignment based on modulus operation
    }

    // Output the cluster assignments
    std::cout << "Cluster initialization." << std::endl;
    std::cout << "There are " << numClusters << " clusters." << std::endl;
    for (int i = 0; i < N; ++i) {
        std::cout << "Data point " << dataPointNames[i] << " is in cluster " << clusterAssignment[i] << std::endl;
    }

    // Create a map of clusters to their data points, store data point's ID inside cluster's vector
    std::vector<std::vector<std::string>> clusters(numClusters);
    for (int i = 0; i < N; ++i) {
        clusters[clusterAssignment[i]].push_back(dataPointNames[i]);
    }

    // Start of balanced K-Means
    // Get the output at current layer (data point allocation for each cluster)
    BalancedKMeans BKM;
    std::vector<std::vector<std::string>> newCluster = BKM.balancedKMeans(N, C, numClusters, clusterAssignment, dataPointNames, clusters, linkCostMatrix);

    // Construct the nodeList for CH allocation
    int i = 0;
    std::cout << "\nIterating new clusters." << std::endl;
    for (const auto& iteCluster: newCluster) {
        std::cout << "Cluster " << i << " contains the following nodes:" <<std::endl;
        for (const auto& iteNode: iteCluster) {
            CHList.erase(std::remove(CHList.begin(), CHList.end(), iteNode), CHList.end());
            std::cout << iteNode << " ";
        }
        std::cout << std::endl;
        ++i;
    }


    std::cout << "\nCurrent CHList for CH allocation later: " << std::endl;
    for (const auto& item : CHList) {
        std::cout << item << std::endl;
    }

    // Start CH allocation, currently ignore those can't find cluster head
    std::vector<std::string> newDataPoints;
    std::cout << "\nStarting CH allocation." << std::endl;
    for (const auto& clusterNodes : newCluster) {
        std::string clusterHead = findCH(clusterNodes, CHList, globalClient);
        if (clusterHead != globalClient) {
            CHList.erase(std::remove(CHList.begin(), CHList.end(), clusterHead), CHList.end());
            aggregationAllocation[clusterHead] = clusterNodes;
            newDataPoints.push_back(clusterHead);
        }
        else {
            std::cout << "Due to no cluster head found, combine these nodes into sub-tree." << std::endl;
            noCHTree.push_back(clusterNodes);
        }

    }

    std::cout << "\nCHList after CH allocation: " << std::endl;
    for (const auto& item : CHList) {
        std::cout << item << std::endl;
    }

    if (newDataPoints.size() < C){
        // If with an entire layer, no CH found, then extract one from subTree list
        if (newDataPoints.empty()){
            const auto& firstSubTree = noCHTree[0];
            aggregationAllocation[globalClient] = firstSubTree;
            noCHTree.erase(noCHTree.begin());
            return true;
        } else {
            aggregationAllocation[globalClient] = newDataPoints;
            return true;
        }
    } else {
        aggregationTreeConstruction(newDataPoints, C);
    }
}
