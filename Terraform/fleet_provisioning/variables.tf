variable "region" {
  description = "The AWS region where resources will be created."
  type        = string
  default     = "us-east-1"
}

variable "function_name" {
  type        = string
  description = "Name of the Lambda Function"
  default     = "FleetProvisioningValidation"
}

variable "description" {
  description = "Description of the Lambda function"
  type        = string
  default     = "Lambda function to validate the devices provisioning"
}

variable "runtime" {
  description = "The identifier of the function's runtime"
  type        = string
  default     = "python3.12"
}

variable "handler" {
  description = "Handler for the Lambda function (e.g., your_module.lambda_handler)"
  type        = string
  default     = "fleet_provisioning_validation.lambda_handler"
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
  default     = "lambda_code/fleet_provisioning_validation.py"
}

variable "provisioning_policy_name" {
  description = "Name of the IoT provisioning policy"
  type        = string
  default     = "iot_provisioning_policy"
}

variable "device_policy_name" {
  description = "Name of the IoT device policy"
  type        = string
  default     = "esp32-policy"
}

variable "iot_provisioning_role_name" {
  description = "The name of the IAM role for IoT provisioning"
  type        = string
  default     = "esp32-fleet-provisioning"
}


variable "template_name" {
  description = "Name of the IoT provisioning template"
  type        = string
  default     = "FleetProvisioning"
}

variable "template_description" {
  description = "Description of the IoT provisioning template"
  type        = string
  default     = "Provisioning template for IoT devices"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE A DYNAMODB TABLE FOR STORING DEVICE'S MAC ADDRESS
# ---------------------------------------------------------------------------------------------------------------------

variable "dynamodb_table_name" {
  description = "Name of the DynamoDB table"
  type        = string
  default     = "OnBoardingDevices"
}

variable "billing_mode" {
  description = "Billing mode for the DynamoDB table. Either PAY_PER_REQUEST or PROVISIONED."
  type        = string
  default     = "PROVISIONED"
}

variable "read_capacity" {
  description = "Read capacity for the table (only used if billing mode is PROVISIONED)"
  type        = number
  default     = 5
}

variable "write_capacity" {
  description = "Write capacity for the table (only used if billing mode is PROVISIONED)"
  type        = number
  default     = 5
}

variable "environment" {
  description = "Environment setting for the Lambda function"
  type        = string
  default     = "prod"
}