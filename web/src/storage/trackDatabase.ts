import { openDB, type DBSchema, type IDBPDatabase } from 'idb'
import type { RemoteTrackInfo, StoredTrack } from '../types'

interface StoredBlock {
  trackId: number
  offset: number
  data: ArrayBuffer
}

interface TrackerDatabase extends DBSchema {
  tracks: {
    key: number
    value: StoredTrack
  }
  blocks: {
    key: [number, number]
    value: StoredBlock
    indexes: { 'by-track': number }
  }
}

let databasePromise: Promise<IDBPDatabase<TrackerDatabase>> | undefined

function database(): Promise<IDBPDatabase<TrackerDatabase>> {
  databasePromise ??= openDB<TrackerDatabase>('c6-tracker-v2', 1, {
    upgrade(db) {
      db.createObjectStore('tracks', { keyPath: 'trackId' })
      const blocks = db.createObjectStore('blocks', { keyPath: ['trackId', 'offset'] })
      blocks.createIndex('by-track', 'trackId')
    },
  })
  return databasePromise
}

export async function mergeRemoteTracks(remote: RemoteTrackInfo[]): Promise<StoredTrack[]> {
  const db = await database()
  const tx = db.transaction('tracks', 'readwrite')
  for (const info of remote) {
    const existing = await tx.store.get(info.trackId)
    if (existing && existing.bytesReceived > info.fileSize) {
      throw new Error(`Файл трека ${info.trackId} на устройстве стал короче локальной копии`)
    }
    await tx.store.put({
      ...info,
      bytesReceived: existing?.bytesReceived ?? 0,
      updatedAt: existing?.updatedAt ?? 0,
    })
  }
  await tx.done
  return db.getAll('tracks')
}

export async function listStoredTracks(): Promise<StoredTrack[]> {
  const db = await database()
  return (await db.getAll('tracks')).sort((a, b) => b.trackId - a.trackId)
}

export async function appendTrackBlock(
  trackId: number,
  offset: number,
  data: Uint8Array,
  fileSize: number,
): Promise<void> {
  const db = await database()
  const tx = db.transaction(['tracks', 'blocks'], 'readwrite')
  const tracks = tx.objectStore('tracks')
  const blocks = tx.objectStore('blocks')
  const meta = await tracks.get(trackId)
  if (!meta) throw new Error(`Неизвестный трек ${trackId}`)
  if (offset !== meta.bytesReceived) {
    throw new Error(`Ожидалось смещение ${meta.bytesReceived}, получено ${offset}`)
  }
  if (offset + data.byteLength > fileSize) throw new Error('Блок выходит за границу файла')
  const copy = new Uint8Array(data.byteLength)
  copy.set(data)
  await blocks.add({ trackId, offset, data: copy.buffer })
  await tracks.put({
    ...meta,
    fileSize,
    bytesReceived: offset + data.byteLength,
    updatedAt: Date.now(),
  })
  await tx.done
}

export async function readTrackFile(trackId: number): Promise<Uint8Array> {
  const db = await database()
  const meta = await db.get('tracks', trackId)
  if (!meta || meta.bytesReceived !== meta.fileSize) throw new Error('Локальная копия трека неполная')
  const blocks = await db.getAllFromIndex('blocks', 'by-track', trackId)
  blocks.sort((a, b) => a.offset - b.offset)
  const output = new Uint8Array(meta.fileSize)
  let expectedOffset = 0
  for (const block of blocks) {
    if (block.offset !== expectedOffset) throw new Error('В локальном файле обнаружен разрыв')
    const data = new Uint8Array(block.data)
    output.set(data, block.offset)
    expectedOffset += data.byteLength
  }
  if (expectedOffset !== output.byteLength) throw new Error('Локальный файл собран не полностью')
  return output
}
