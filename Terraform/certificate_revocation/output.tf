output "lambda_function_arn" {
  description = "ARN of the Lambda function"
  value       = aws_lambda_function.lambda_function.arn
}

output "lambda_function_name" {
  description = "Name of the Lambda function"
  value       = aws_lambda_function.lambda_function.function_name
}

output "iot_topic_rule_details" {
  description = "Details of the IoT Topic Rule"
  value = {
    rule_name = aws_iot_topic_rule.rule.name
    rule_arn  = aws_iot_topic_rule.rule.arn
    sql       = aws_iot_topic_rule.rule.sql
  }
}