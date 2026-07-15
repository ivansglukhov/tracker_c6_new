export interface DeviceStatus {
  flags: number
  trackId: number
  pointCount: number
  distanceM: number
  altitudeM: number
  awakeElapsedSec: number
  awakeTimeSec: number
  interactiveRemainingSec: number
  batteryPercent: number
  satellites: number
  wakeReason: number
}

export interface LivePoint {
  trackId: number
  sampleId: number
  gpsEpoch: number
  latitude: number
  longitude: number
  altitudeM: number
  satellites: number
  flags: number
}

export interface DeviceSettings {
  awakeTimeSec: number
  sleepTimeSec: number
  screenOnTimerWake: boolean
  bleOnTimerWake: boolean
  followSleepScheduleWhileBle: boolean
}

export interface GuidancePoint {
  latitude: number
  longitude: number
  name?: string
}

export interface GuidanceRoute {
  name: string
  type: 'line' | 'points'
  points: GuidancePoint[]
}

export interface RemoteTrackInfo {
  trackId: number
  fileSize: number
  createdEpoch: number
  current: boolean
}

export interface StoredTrack extends RemoteTrackInfo {
  bytesReceived: number
  updatedAt: number
}

export interface TrackPoint {
  sequence: number
  gpsEpoch: number
  latitude: number
  longitude: number
  altitudeM: number
  speedMps: number
  courseDeg: number
  hdop: number
  satellites: number
  flags: number
  wakeCycleId: number
  cumulativeDistanceM: number
}

export interface TrackFilterSettings {
  minSatellites: number
  maxHdop: number
  maxJumpM: number
}
