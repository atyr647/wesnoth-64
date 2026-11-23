/*
 * vorbisfile.h -- the part of libvorbisfile Wesnoth uses (reading a track's
 * comment tags for its title), for the Nintendo 64.
 *
 * Audio is decoded by SDL_mixer (bundled stb_vorbis); only the tags are read
 * here, straight from the Vorbis comment header in the file's first pages.
 */
#pragma once
#include "n64_vfs.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

typedef struct vorbis_comment {
    char** user_comments;
    int* comment_lengths;
    int comments;
    char* vendor;
} vorbis_comment;

typedef struct OggVorbis_File {
    vorbis_comment vc;
    std::vector<std::string>* store;
} OggVorbis_File;

namespace n64_vorbisfile {
inline unsigned le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

// The comment header packet may span Ogg pages; join the page payloads of
// the first few pages (headers are small) and parse the packet.
inline bool read_comments(const std::string& file, std::vector<std::string>& out, std::string& vendor)
{
    std::string payload;
    size_t pos = 0;
    for (int page = 0; page < 8 && pos + 27 <= file.size(); ++page) {
        if (file.compare(pos, 4, "OggS") != 0) return false;
        const unsigned nseg = (unsigned char)file[pos + 26];
        if (pos + 27 + nseg > file.size()) return false;
        size_t len = 0;
        for (unsigned i = 0; i < nseg; ++i) len += (unsigned char)file[pos + 27 + i];
        payload.append(file, pos + 27 + nseg, len);
        pos += 27 + nseg + len;
    }
    const size_t h = payload.find("\x03vorbis", 0, 7);
    if (h == std::string::npos) return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(payload.data()) + h + 7;
    const unsigned char* end = reinterpret_cast<const unsigned char*>(payload.data()) + payload.size();
    if (p + 4 > end) return false;
    unsigned n = le32(p); p += 4;
    if (p + n > end) return false;
    vendor.assign(reinterpret_cast<const char*>(p), n); p += n;
    if (p + 4 > end) return false;
    unsigned count = le32(p); p += 4;
    for (unsigned i = 0; i < count && p + 4 <= end; ++i) {
        n = le32(p); p += 4;
        if (p + n > end) break;
        out.emplace_back(reinterpret_cast<const char*>(p), n);
        p += n;
    }
    return true;
}
} // namespace n64_vorbisfile

inline int ov_fopen(const char* path, OggVorbis_File* vf)
{
    std::memset(vf, 0, sizeof *vf);
    auto r = n64vfs::open_read(path);
    if (!r) return -1;
    std::string head(65536, '\0');
    head.resize(r->read(head.data(), head.size()));
    auto* store = new std::vector<std::string>();
    std::string vendor;
    if (!n64_vorbisfile::read_comments(head, *store, vendor)) { delete store; return -1; }
    store->push_back(vendor);                 // last entry: vendor string
    vf->store = store;
    vf->vc.comments = (int)store->size() - 1;
    vf->vc.user_comments = static_cast<char**>(std::calloc(store->size(), sizeof(char*)));
    vf->vc.comment_lengths = static_cast<int*>(std::calloc(store->size(), sizeof(int)));
    for (size_t i = 0; i + 1 < store->size(); ++i) {
        vf->vc.user_comments[i] = (*store)[i].data();
        vf->vc.comment_lengths[i] = (int)(*store)[i].size();
    }
    vf->vc.vendor = store->back().data();
    return 0;
}

inline vorbis_comment* ov_comment(OggVorbis_File* vf, int) { return &vf->vc; }

inline int ov_clear(OggVorbis_File* vf)
{
    std::free(vf->vc.user_comments);
    std::free(vf->vc.comment_lengths);
    delete vf->store;
    std::memset(vf, 0, sizeof *vf);
    return 0;
}
