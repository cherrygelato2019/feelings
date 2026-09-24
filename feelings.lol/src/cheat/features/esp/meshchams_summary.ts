/**
 * meshchams — MeshContentProvider LRU layout summary
 *
 * Read-only. Walk lruCache1 only (+0x20). Ignore lruCache2 (+0x28).
 */

export interface MeshContentProvider {
  /** +0xE8 → CacheObject* */
  cacheObject: pointer;
}

export interface CacheObject {
  /** +0x20 → real SizeEnforced / MemEnforced LRU */
  lruCache1: pointer;
  /** +0x28 → deprecated — ignore */
  lruCache2: pointer;
}

export interface LruCache1 {
  /** +0x08 → sentinel (doubly linked list head) */
  sentinel: pointer;
}

/** ~72 bytes per node. sentinel.next walks newest-first. */
export interface LruNode {
  next: pointer; // +0x00
  prev: pointer; // +0x08
  contentId: msvc_string; // +0x10
  meshData: pointer; // +0x30 shared_ptr<FileMeshData>::ptr
  controlBlock: pointer; // +0x38
}

export interface FileMeshData {
  vertexBegin: pointer; // +0x00
  vertexEnd: pointer; // +0x08
  faceBegin: pointer; // +0x30
  faceEnd: pointer; // +0x38
}

/** vertex stride 40 */
export interface Vertex {
  pos: [number, number, number];
  normal: [number, number, number];
  uv: [number, number];
  tangent: number; // u32, default 0xFE7F7F7F
  color: number; // u32, default 0xFFFFFFFF
}

/** face stride 12 — three u32 indices */
export type Face = [number, number, number];

export const counts = {
  vertexCount: "(vertex_end - vertex_begin) / 40",
  faceCount: "(face_end - face_begin) / 12",
} as const;

/**
 * Usage:
 * 1. MeshContentProvider (DataModel child / service)
 * 2. +0xE8 CacheObject → +0x20 lruCache1 → +0x08 sentinel
 * 3. walk sentinel.next; match content_id to MeshPart/SpecialMesh MeshId
 * 4. read FileMeshData verts/faces; transform by part CFrame + size; draw
 *
 * Notes:
 * - lruCache1 = real data; lruCache2 = useless
 * - newest meshes near list head
 * - content_id for naming / matching
 * - shared_ptr may keep data briefly after eviction
 * - MeshFetch hook is cleaner for realtime dumps; LRU walk is enough for ESP
 */

type pointer = number;
type msvc_string = string;
