#ifndef ABSL_BASE_NULLABILITY_H_
#define ABSL_BASE_NULLABILITY_H_

// Stub: absl_nonnull is a Clang nullability annotation.
// In non-Clang builds it's defined as empty.
#ifndef absl_nonnull
// Disabled for Android NDK (Clang 18+) which rejects _Nonnull on non-pointer
// types like std::unique_ptr.
#if defined(__clang__) && defined(__has_feature) && !defined(ANDROID)
#if __has_feature(nullability)
#define absl_nonnull _Nonnull
#else
#define absl_nonnull
#endif
#else
#define absl_nonnull
#endif
#endif

#endif  // ABSL_BASE_NULLABILITY_H_