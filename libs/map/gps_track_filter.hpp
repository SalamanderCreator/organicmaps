#pragma once

#include "platform/location.hpp"

#include <vector>

/// Maps&Fly: builds the saved track from all recorded fixes. Jumps faster than a plausible speed are dropped. Then
/// fixes are taken from the most accurate one on, and a fix is kept if it is farther than its own accuracy from each
/// kept fix next to it in time. Returns the kept fixes in time order.
std::vector<location::GpsInfo> SelectAccurateTrackPoints(std::vector<location::GpsInfo> points);

class IGpsTrackFilter
{
public:
  virtual ~IGpsTrackFilter() = default;

  using GpsVectorT = std::vector<location::GpsInfo>;
  virtual void Process(GpsVectorT const & inPoints, GpsVectorT & outPoints) = 0;
  virtual void Finalize(GpsVectorT & outPoints) {}
  virtual void Reset() {}
};

class GpsTrackNullFilter : public IGpsTrackFilter
{
public:
  // IGpsTrackFilter overrides
  void Process(GpsVectorT const & inPoints, GpsVectorT & outPoints) override;
};

class GpsTrackFilter : public IGpsTrackFilter
{
public:
  /// Store setting for  minimal horizontal accuracy
  static void StoreMinHorizontalAccuracy(double value);
  static double LoadMinHorizontalAccuracy();

  GpsTrackFilter();

  // IGpsTrackFilter overrides
  void Process(GpsVectorT const & inPoints, GpsVectorT & outPoints) override;
  void Finalize(GpsVectorT & outPoints) override;
  void Reset() override;

private:
  bool IsGoodPoint(location::GpsInfo const & info) const;
  bool IsGoodVector(location::GpsInfo const & info) const;

  location::GpsInfo const & GetLastInfo() const;
  location::GpsInfo const & GetLastAcceptedInfo() const;

  void AddLastInfo(location::GpsInfo const & info);

  double m_minAccuracy;

  location::GpsInfo m_lastInfo[2];
  size_t m_countLastInfo = 0;

  location::GpsInfo m_lastAcceptedInfo;
};
