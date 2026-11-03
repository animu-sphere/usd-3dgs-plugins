// SPDX-License-Identifier: Apache-2.0
//
// A test-only URI resolver for the `gstest:` scheme, built and registered only
// by gaussian-sog's CTest suite (never installed or packaged). It drives the
// RESOLVER_MATRIX.md rows that ArDefaultResolver cannot reach:
//
// - a URI-style primary identifier (`gstest:cloud/meta.json`) that no
//   filesystem call could open, so the plugin must read it through ArAsset;
// - companion planes served by a custom resolver: the plugin anchors
//   `./<plane>` on the URI, Ar dispatches that to this resolver, and this
//   resolver alone decides what it names;
// - an asset that serves a stream only: GetBuffer() returns null, so the
//   plugin must fall back to ArAsset::Read().
//
// The identifiers name entries of a flat store rooted at the directory in
// GSTEST_URI_RESOLVER_ROOT (`gstest:a/b.webp` is `<root>/a/b.webp`). That
// mapping is private to this resolver; the plugin only ever sees the URI.
// When GSTEST_URI_RESOLVER_LOG names a file, every OpenAsset and GetBuffer
// call appends one line to it (`open <identifier>`, `buffer <identifier>`),
// so the Python suite can assert which assets were requested and how.

#include "pxr/pxr.h"

#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/defineResolver.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/timestamp.h"
#include "pxr/usd/ar/writableAsset.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

constexpr char kScheme[] = "gstest:";
constexpr std::size_t kSchemeLength = sizeof(kScheme) - 1;

bool HasScheme(const std::string& path)
{
    return path.compare(0, kSchemeLength, kScheme) == 0;
}

void LogLine(const std::string& line)
{
    static std::mutex mutex;
    const std::string logPath = TfGetenv("GSTEST_URI_RESOLVER_LOG");
    if (logPath.empty()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mutex);
    std::ofstream log(logPath, std::ios::out | std::ios::app);
    log << line << '\n';
}

// Normalizes a store path ('/'-separated, relative to the store root): drops
// empty and "." segments and folds "..". Fails on a "..", that would leave
// the store, and on a '\' anywhere, so a store path never names anything the
// store does not hold.
bool NormalizeStorePath(const std::string& path, std::string* normalized)
{
    if (path.find('\\') != std::string::npos) {
        return false;
    }
    std::vector<std::string> segments;
    for (const std::string& segment : TfStringSplit(path, "/")) {
        if (segment.empty() || segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (segments.empty()) {
                return false;
            }
            segments.pop_back();
            continue;
        }
        segments.push_back(segment);
    }
    if (segments.empty()) {
        return false;
    }
    *normalized = TfStringJoin(segments, "/");
    return true;
}

std::string StoreFile(const std::string& identifier)
{
    const std::string root = TfGetenv("GSTEST_URI_RESOLVER_ROOT");
    if (root.empty() || !HasScheme(identifier)) {
        return std::string();
    }
    return root + "/" + identifier.substr(kSchemeLength);
}

// Serves the bytes it was opened with, as a stream only: GetBuffer() returns
// null and GetFileUnsafe() has no FILE*, as for a resolver that downloads or
// decrypts its assets.
class GsTestStreamAsset : public ArAsset {
public:
    GsTestStreamAsset(std::string identifier, std::vector<char> bytes)
        : _identifier(std::move(identifier)), _bytes(std::move(bytes))
    {}

    size_t GetSize() const override { return _bytes.size(); }

    std::shared_ptr<const char> GetBuffer() const override
    {
        LogLine("buffer " + _identifier);
        return nullptr;
    }

    size_t Read(void* buffer, size_t count, size_t offset) const override
    {
        if (offset >= _bytes.size()) {
            return 0;
        }
        const size_t available = _bytes.size() - offset;
        const size_t n = std::min(count, available);
        std::memcpy(buffer, _bytes.data() + offset, n);
        return n;
    }

    std::pair<FILE*, size_t> GetFileUnsafe() const override
    {
        return std::pair<FILE*, size_t>(nullptr, 0);
    }

private:
    std::string _identifier;
    std::vector<char> _bytes;
};

} // namespace

class GsTestUriResolver : public ArResolver {
public:
    GsTestUriResolver() = default;
    ~GsTestUriResolver() override = default;

protected:
    // An identifier with the scheme is absolute: it is normalized and the
    // anchor is ignored. Anything else reaches this resolver only because its
    // anchor is a `gstest:` identifier (Ar dispatches a scheme-less path to
    // its anchor's resolver), and is anchored on the anchor's directory,
    // RFC 3986 style: a leading '/' starts at the store root.
    std::string
    _CreateIdentifier(const std::string& assetPath,
                      const ArResolvedPath& anchorAssetPath) const override
    {
        std::string normalized;
        if (HasScheme(assetPath)) {
            return NormalizeStorePath(assetPath.substr(kSchemeLength),
                                      &normalized) ?
                       kScheme + normalized :
                       std::string();
        }
        const std::string& anchor = anchorAssetPath.GetPathString();
        if (!HasScheme(anchor)) {
            return assetPath;
        }
        std::string base;
        if (assetPath.empty() || assetPath[0] != '/') {
            const std::string anchorPath = anchor.substr(kSchemeLength);
            const std::string::size_type slash = anchorPath.find_last_of('/');
            if (slash != std::string::npos) {
                base = anchorPath.substr(0, slash + 1);
            }
        }
        return NormalizeStorePath(base + assetPath, &normalized) ?
                   kScheme + normalized :
                   std::string();
    }

    std::string
    _CreateIdentifierForNewAsset(const std::string& assetPath,
                                 const ArResolvedPath& anchorAssetPath)
        const override
    {
        return _CreateIdentifier(assetPath, anchorAssetPath);
    }

    ArResolvedPath _Resolve(const std::string& assetPath) const override
    {
        std::string normalized;
        if (!HasScheme(assetPath) ||
            !NormalizeStorePath(assetPath.substr(kSchemeLength),
                                &normalized)) {
            return ArResolvedPath();
        }
        const std::string identifier = kScheme + normalized;
        const std::string file = StoreFile(identifier);
        if (file.empty() || !TfIsFile(file)) {
            return ArResolvedPath();
        }
        return ArResolvedPath(identifier);
    }

    ArResolvedPath
    _ResolveForNewAsset(const std::string& assetPath) const override
    {
        (void)assetPath;
        return ArResolvedPath();
    }

    ArTimestamp
    _GetModificationTimestamp(const std::string& assetPath,
                              const ArResolvedPath& resolvedPath) const override
    {
        (void)assetPath;
        (void)resolvedPath;
        return ArTimestamp(0.0);
    }

    std::shared_ptr<ArAsset>
    _OpenAsset(const ArResolvedPath& resolvedPath) const override
    {
        const std::string& identifier = resolvedPath.GetPathString();
        LogLine("open " + identifier);
        const std::string file = StoreFile(identifier);
        if (file.empty()) {
            return nullptr;
        }
        std::ifstream stream(file, std::ios::in | std::ios::binary);
        if (!stream) {
            return nullptr;
        }
        std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
        if (stream.bad()) {
            return nullptr;
        }
        return std::make_shared<GsTestStreamAsset>(identifier,
                                                   std::move(bytes));
    }

    std::shared_ptr<ArWritableAsset>
    _OpenAssetForWrite(const ArResolvedPath& resolvedPath,
                       WriteMode writeMode) const override
    {
        (void)resolvedPath;
        (void)writeMode;
        return nullptr;
    }
};

AR_DEFINE_RESOLVER(GsTestUriResolver, ArResolver);

PXR_NAMESPACE_CLOSE_SCOPE
