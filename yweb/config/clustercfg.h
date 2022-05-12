#pragma once

#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/system/defaults.h>

class TClusterCount {
public:
    explicit TClusterCount(int clusters)
        : NumClusters(clusters)
    {
    }

    int Clusters() const {
        return NumClusters;
    }

protected:
    int NumClusters;
};

class ClusterConfig : public TClusterCount {

public:
    ClusterConfig()
        : TClusterCount(42)
    {
    }

    explicit ClusterConfig(const int clusters)
        : TClusterCount(clusters)
    {
    }

    ClusterConfig(const char *fname, const char *segment = nullptr)
        : TClusterCount(42)
    {
        LoadFromConfig(fname, segment);
    }

    void Handle2Cluster(const ui32 handle, ui32 &cluster, ui32 &chandle) const {
        cluster = handle % NumClusters;
        chandle = handle / NumClusters;
    }

    void Cluster2Handle(ui32 &handle, const ui32 cluster, const ui32 chandle) const {
        handle = chandle * NumClusters + cluster;
    }

    void LoadFromConfig(const char *fname, const char *segment = nullptr);
    void LoadFromConfig(const class THostConfig &cfg, const char *segment = nullptr);
};

class SearchClusterConfig {
public:
    struct TZoneConfig {
        const char *ZoneName;
        const TClusterCount Conf;

        TZoneConfig(const char* &zoneName, const TClusterCount &config)
            : ZoneName(zoneName)
            , Conf(config)
        {}

        inline bool Eq(const TZoneConfig &rhs) const {
            return !strcmp(this->ZoneName, rhs.ZoneName);
        }

        inline bool Eq(const char* rhs) const {
            return !strcmp(this->ZoneName, rhs);
        }
    };

public:
    inline SearchClusterConfig(const TVector<TZoneConfig>& configs)
        : Configs(configs)
    {
        RebuildClusterIndexes();
    }

    inline int UploadClusters() const {
        return UploadClustersCount;
    }

    inline int Clusters() const {
        return (int)Cluster2ZoneIndex.size();
    }

    inline int Clusters(const char* searchZoneName) const {
        const TZoneConfig& zoneConf = Configs[FindZoneConfig(searchZoneName)];
        return zoneConf.Conf.Clusters();
    }

    inline const char* GetSearchZoneName(const ui32 cluster) const {
        ui32 zoneNum = Cluster2ZoneIndex[cluster];
        return Configs[zoneNum].ZoneName;
    }

    inline ui32 GetZoneClusterOffset(const char* SearchZoneName) const {
        size_t index = FindZoneConfig(SearchZoneName);
        return Zone2ClusterIndex[index];
    }

private:
    TVector<TZoneConfig> Configs;
    TVector<ui32> Zone2ClusterIndex;
    TVector<ui32> Cluster2ZoneIndex;
    int           UploadClustersCount;

private:
    void RebuildClusterIndexes();

    inline size_t FindZoneConfig(const char* SearchZoneName) const {
        for (size_t i = 0; i < Configs.size(); ++i) {
            const TZoneConfig& zoneConf = Configs[i];
            if (zoneConf.Eq(SearchZoneName)) {
                return i;
            }
        }

        ythrow yexception() << "Zone " <<  SearchZoneName << " is not configured for search clusters";
    }
};
