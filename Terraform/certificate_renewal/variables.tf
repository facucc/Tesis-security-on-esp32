variable "region" {
  description = "The AWS region where resources will be created"
  type        = string
  default     = "us-east-1"
}

# ---------------------------------------------------------------------------------------------------------------------
# CERTIFICATE RENEWAL LAMBDA FUNCTION VARIABLES
# ---------------------------------------------------------------------------------------------------------------------

variable "cert_renewal_function_name" {
  type        = string
  description = "Name of the Lambda Function"
  default     = "LambdaCertificateRenewal"
}

variable "cert_renewal_description" {
  description = "Description of the Lambda function"
  type        = string
  default     = "Lambda function for certificate renewal"
}

variable "runtime" {
  description = "The identifier of the function's runtime"
  type        = string
  default     = "python3.12"
}

variable "cert_renewal_handler" {
  description = "Handler for the Lambda function (e.g., your_module.lambda_handler)"
  type        = string
  default     = "certificate_renewal.lambda_handler"
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

variable "cert_renewal_filename_path" {
  description = "The path to the function's deployment package within the local filesystem."
  type        = string
  default     = "lambda_code/certificate_renewal.py"
}

# AWS IoT Rules settings

variable "rule_name" {
  type        = string
  description = "A name for the aws iot rule"
  default     = "RuleCertificateRenewal"
}

variable "accepted_topic" {
  description = "Accepted topic for the IoT certificate"
  type        = string
  default     = "things/thingName/certificate/create-from-csr/json/accepted"
}

variable "rejected_topic" {
  description = "Rejected topic for the IoT certificate"
  type        = string
  default     = "things/thingName/certificate/create-from-csr/json/rejected"
}

# ---------------------------------------------------------------------------------------------------------------------
# CERTIFICATE RENEWAL REQUEST LAMBDA FUNCTION VARIABLES
# ---------------------------------------------------------------------------------------------------------------------

variable "cert_renewal_request_function_name" {
  type        = string
  description = "Name of the Request Renewal Lambda Function"
  default     = "LambdaCertificateRenewalRequest"
}

variable "cert_renewal_request_description" {
  description = "Description of the Request Renewal Lambda function"
  type        = string
  default     = "Lambda function for requesting certificate renewal"
}

variable "cert_renewal_request_handler" {
  description = "Handler for the Request Renewal Lambda function (e.g., your_module.lambda_handler)"
  type        = string
  default     = "certificate_renewal_request.lambda_handler"
}

variable "cert_renewal_request_filename_path" {
  description = "The path to the Request Renewal function's deployment package within the local filesystem."
  type        = string
  default     = "lambda_code/certificate_renewal_request.py"
}

variable "bucket_name" {
  type    = string
  default = "jobs-document"

}

variable "environment" {
  description = "Environment setting for the aws resources"
  type        = string
  default     = "prod"
}

