variable "region" {
  description = "The AWS region where resources will be created."
  type        = string
  default     = "us-east-1"
}

variable "function_name" {
  type        = string
  description = "Name of the Lambda Function"
  default     = "UploadImages"
}

variable "description" {
  description = "Description of the Lambda function"
  type        = string
  default     = "Lambda function to upload images"
}

variable "runtime" {
  description = "The identifier of the function's runtime"
  type        = string
  default     = "python3.12"
}

variable "handler" {
  description = "Handler for the Lambda function (e.g., your_module.lambda_handler)"
  type        = string
  default     = "uploadImages.lambda_handler"
}

variable "architectures" {
  description = "Instruction set architecture for your Lambda function. Valid values are x86_64 and arm64. Default is x86_64. Removing this attribute, function's architecture stay the same."
  type        = list(string)
  default     = ["x86_64"]
}

variable "memory_size" {
  description = "Memory size for the Lambda function in MB"
  type        = number
  default     = 128
}

variable "timeout" {
  description = "Timeout for the Lambda function in seconds"
  type        = number
  default     = 10
}

variable "filename_path" {
  description = "The path to the function's deployment package within the local filesystem."
  type        = string
  default     = "lambda_code/uploadImages.py"
}

variable "bucket_name" {
  type    = string
  default = "tesis-pictures"

}

# AWS IoT Rules settings

variable "rule_name" {
  type        = string
  description = "A name for the aws iot rule"
  default     = "UploadImages"
}

variable "time_zone" {
  description = "Time zone"
  type        = string
  default     = "America/Argentina/Buenos_Aires"
}

variable "environment" {
  description = "Environment setting for the Lambda function"
  type        = string
  default     = "prod"
}

