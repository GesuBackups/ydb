#include <util/generic/yexception.h>

#include "clustercfg.h"
#include "hostconfig.h"

void ClusterConfig::LoadFromConfig(const char *fname, const char *segment)
{
    THostConfig HostConfig;
    if (HostConfig.Load(fname))
        ythrow yexception() << "Can't load \"" << fname << "\": " << LastSystemErrorText();
    LoadFromConfig(HostConfig, segment);
}

void ClusterConfig::LoadFromConfig(const THostConfig &HostConfig, const char *segment) {
    if (segment) {
        if (!HostConfig.GetSegmentClusterConfig(segment))
            ythrow yexception() << "Host " <<  segment << " not configured in " << HostConfig.GetConfigPath();
        *this = *HostConfig.GetSegmentClusterConfig(segment);
        return;
    }
    if (HostConfig.GetMySegmentId() == -1)
        ythrow yexception() << "Host not configured";
    if (!HostConfig.GetSegmentClusterConfig(HostConfig.GetMySegmentId()))
        ythrow yexception() << "Host not configured";
    *this = *HostConfig.GetSegmentClusterConfig(HostConfig.GetMySegmentId());
}

void SearchClusterConfig::RebuildClusterIndexes() {
    int clNum = 0;
    Zone2ClusterIndex.push_back(0);
    UploadClustersCount = 0;

    for (size_t i = 0; i < Configs.size(); ++i) {
        const TClusterCount conf = Configs[i].Conf;
        const char* &zoneName = Configs[i].ZoneName;

        for (int j = 0; j < conf.Clusters(); ++j, ++clNum) {
            Cluster2ZoneIndex.push_back(i);
        }

        Zone2ClusterIndex.push_back(clNum);

        if (zoneName[3] != 'L') {
            UploadClustersCount += conf.Clusters();
        }

    }
}
