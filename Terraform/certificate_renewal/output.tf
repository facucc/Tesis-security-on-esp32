output "cert_renewal_lambda_function_details" {
  description = "Details of the Certificate Renewal Lambda function"
  value = {
    function_name = aws_lambda_function.cert_renewal_lambda_function.function_name
    function_arn  = aws_lambda_function.cert_renewal_lambda_function.arn
  }
}

output "cert_renewal_request_lambda_function_details" {
  description = "Details of the Certificate Renewal Request Lambda function"
  value = {
    function_name = aws_lambda_function.cert_renewal_request_lambda_function.function_name
    function_arn  = aws_lambda_function.cert_renewal_request_lambda_function.arn
  }
}

output "iot_rule_details" {
  description = "Details of the IoT Topic Rule"
  value = {
    rule_name = aws_iot_topic_rule.rule.name
    rule_arn  = aws_iot_topic_rule.rule.arn
  }
}

output "bucket_name" {
  description = "Name of the bucket for storing job documents"
  value       = aws_s3_bucket.job_document.bucket
}