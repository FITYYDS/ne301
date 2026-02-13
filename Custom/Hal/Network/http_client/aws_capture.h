/**
 * @file aws_capture.h
 * @brief AWS S3 capture upload test: init (region, bucket, AK, SK) and upload (capture + PUT).
 */

#ifndef __AWS_CAPTURE_H__
#define __AWS_CAPTURE_H__

#ifdef __cplusplus
extern "C" {
#endif

/** Register shell commands: aws_capture init <region> <bucket> <AK> <SK>, aws_capture upload */
void aws_capture_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __AWS_CAPTURE_H__ */
