/**
 * @file test_util.h
 * @brief 极简断言框架。无外部依赖，任何 C99 编译器可用。
 */
#ifndef VK_TEST_UTIL_H
#define VK_TEST_UTIL_H

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int vk_tests_run;
extern int vk_tests_failed;
extern const char *vk_suite_name;

#define VK_SUITE(name) static void suite_##name(void)

#define VK_CHECK(cond)                                                      \
    do {                                                                    \
        vk_tests_run++;                                                     \
        if (!(cond)) {                                                      \
            vk_tests_failed++;                                              \
            (void)printf("    FAIL %s:%d  (%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

#define VK_CHECK_EQ_INT(a, b)                                               \
    do {                                                                    \
        long va_ = (long)(a);                                               \
        long vb_ = (long)(b);                                               \
        vk_tests_run++;                                                     \
        if (va_ != vb_) {                                                   \
            vk_tests_failed++;                                              \
            (void)printf("    FAIL %s:%d  %s == %s  (got %ld, want %ld)\n", \
                         __FILE__, __LINE__, #a, #b, va_, vb_);             \
        }                                                                   \
    } while (0)

#define VK_CHECK_EQ_STR(a, b)                                               \
    do {                                                                    \
        const char *sa_ = (a);                                              \
        const char *sb_ = (b);                                              \
        vk_tests_run++;                                                     \
        if ((sa_ == NULL) || (sb_ == NULL) || (strcmp(sa_, sb_) != 0)) {     \
            vk_tests_failed++;                                              \
            (void)printf("    FAIL %s:%d  %s == %s  (got \"%s\", want \"%s\")\n", \
                         __FILE__, __LINE__, #a, #b,                        \
                         (sa_ ? sa_ : "(null)"), (sb_ ? sb_ : "(null)"));   \
        }                                                                   \
    } while (0)

#define VK_CHECK_MEM(a, b, n)                                               \
    do {                                                                    \
        vk_tests_run++;                                                     \
        if (memcmp((a), (b), (n)) != 0) {                                    \
            vk_tests_failed++;                                              \
            (void)printf("    FAIL %s:%d  memcmp(%s, %s, %u) differs\n",    \
                         __FILE__, __LINE__, #a, #b, (unsigned)(n));        \
        }                                                                   \
    } while (0)

#define VK_CHECK_HEX(a, n)                                                  \
    do {                                                                    \
        size_t i_;                                                          \
        (void)printf("        hex:");                                       \
        for (i_ = 0u; i_ < (size_t)(n); i_++) {                              \
            (void)printf(" %02X", ((const unsigned char *)(a))[i_]);        \
        }                                                                   \
        (void)printf("\n");                                                 \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* VK_TEST_UTIL_H */
