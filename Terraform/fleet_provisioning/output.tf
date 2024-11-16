output "lambda_function_arn" {
  description = "ARN of the Lambda function"
  value       = aws_lambda_function.lambda_function.arn
}

output "lambda_function_name" {
  description = "Name of the Lambda function"
  value       = aws_lambda_function.lambda_function.function_name
}

output "dynamodb_table_details" {
  description = "Details of the DynamoDB table for device onboarding"
  value = {
    table_name = aws_dynamodb_table.onboarding_devices.name
    table_arn  = aws_dynamodb_table.onboarding_devices.arn
  }
}

output "iot_provisioning_template_details" {
  description = "Details of the IoT provisioning template"
  value = {
    template_name = aws_iot_provisioning_template.template.name
    template_arn  = aws_iot_provisioning_template.template.arn
  }
}