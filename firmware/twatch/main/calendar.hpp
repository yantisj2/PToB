// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch::calendar {
constexpr double Latitude=30.35,Longitude=-86.23; // Representative Santa Rosa Beach Gulf coast.
constexpr const char* TideStation="8729511";
enum class Coverage:uint8_t {ClockUnknown,Available,BeforeRange,Expired};
enum class Light:uint8_t {Unknown,Night,Dawn,Day,Dusk};
struct Date {int year=0;unsigned month=0,day=0;};
struct SolarDay {Date date{};uint64_t dawn=0,sunrise=0,noon=0,sunset=0,dusk=0;uint64_t astronomical_dawn=0,nautical_dawn=0,nautical_dusk=0,astronomical_dusk=0;bool valid=false;};
struct Tide {uint64_t utc=0;int16_t height_mm=0;bool high=false;};
struct Snapshot {
 uint64_t sampled_utc=0,next_sunrise=0,next_sunset=0,tides_until=0;
 SolarDay today{};Tide next_tides[8]{};uint8_t tide_count=0;
 Coverage tides=Coverage::ClockUnknown;Light light=Light::Unknown;
 int16_t local_offset_minutes=0;uint16_t daylight_permille=0;
 double solar_altitude_deg=0,solar_azimuth_deg=0,solar_hour_angle_deg=0;
 double moon_phase=0,moon_illumination=0,moon_altitude_deg=0,moon_azimuth_deg=0;
 bool astronomy_valid=false;
 Tide previous_tide{};bool previous_tide_valid=false;
 double tide_position=0;bool tide_position_valid=false,tide_rising=false;
 bool clock_estimated=false;
};
// Fixed-location Central time, current US DST rule (2007 onward), independent
// of the mesh's display zone. Unsupported dates/unknown UTC return unavailable.
uint64_t midnight(Date date);
int16_t central_offset(uint64_t utc);
SolarDay solar(Date local_date);
Snapshot snapshot(uint64_t utc,bool clock_valid,bool estimated=false);
// Published by the UI worker once per UTC minute; consumers must check sampled_utc.
// Display-only hold is marked clock_estimated and never grants timing authority.
bool current(Snapshot& out);
// Future automatic-brightness consumer; no panel writes or setting changes.
// Unknown time preserves the operator's fallback. Twilight is continuous.
uint8_t brightness(const Snapshot&,uint8_t night,uint8_t day,uint8_t fallback);
}
