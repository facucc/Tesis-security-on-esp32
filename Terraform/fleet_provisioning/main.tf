terraform {
  required_version = ">= 1.0.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "5.75.0"
    }
  }
}

provider "aws" {
  region = "${var.region}"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE THE THE DEVICES FLEET PROVISIONING VALIDATION LAMBDA FUNCTION
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_lambda_function" "lambda_function" {
  function_name = var.function_name
  description   = var.description
  runtime       = var.runtime
  handler       = var.handler
  role          = aws_iam_role.lambda_role.arn
  memory_size   = var.memory_size
  timeout       = var.timeout
  publish       = true

  filename         = data.archive_file.lambda.output_path
  source_code_hash = data.archive_file.lambda.output_base64sha256


  environment {
    variables = {
      DynamoDBTable = var.dynamodb_table_name
      environment   = var.environment
    }
  }
}

data "archive_file" "lambda" {
  type        = "zip"
  source_file = var.filename_path
  output_path = "lambda_function.zip"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE AN IAM ROLE FOR THE DEVICES FLEET PROVISIONING VALIDATION LAMBDA FUNCTION
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_iam_role" "lambda_role" {
  name               = var.function_name
  description        = "For lambda function ${var.function_name}"
  assume_role_policy = data.aws_iam_policy_document.lambda_role.json

  lifecycle {
    create_before_destroy = true
  }
}

data "aws_iam_policy_document" "lambda_role" {
  statement {
    effect  = "Allow"
    actions = ["sts:AssumeRole"]

    principals {
      type        = "Service"
      identifiers = ["lambda.amazonaws.com"]
    }
  }
}
# Create a custom IAM policy for IoT permissions based on the defined policy document
resource "aws_iam_policy" "iot_policy" {
  name   = var.function_name
  policy = data.aws_iam_policy_document.iot_policy.json
}

# Define the IoT policy document with necessary permissions
data "aws_iam_policy_document" "iot_policy" {
  statement {
    actions = [
      "dynamodb:GetItem",
      "dynamodb:Query",
      "dynamodb:Scan",
      "dynamodb:UpdateItem"
    ]

    resources = ["${aws_dynamodb_table.onboarding_devices.arn}"]

    effect = "Allow"
  }
}

# Attach the custom IoT policy to the Lambda role
resource "aws_iam_role_policy_attachment" "iot_policy_attachment" {
  role       = aws_iam_role.lambda_role.name
  policy_arn = aws_iam_policy.iot_policy.arn
}

# Attach CloudWatch logging policy to the Lambda role for basic execution permissions
resource "aws_iam_role_policy_attachment" "logging_for_lambda" {
  role       = aws_iam_role.lambda_role.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

data "aws_caller_identity" "current" {}


resource "aws_iot_policy" "provisioning_policy" {
  name = var.provisioning_policy_name

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = "iot:Connect"
        Resource = "*"
      },
      {
        Effect = "Allow"
        Action = [
          "iot:Publish",
          "iot:Receive"
        ]
        Resource = [
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/certificates/create-from-csr/*",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/provisioning-templates/FleetProvisioningDev/provision/*"
        ]
      },
      {
        Effect = "Allow"
        Action = "iot:Subscribe"
        Resource = [
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topicfilter/$aws/certificates/create-from-csr/*",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topicfilter/$aws/provisioning-templates/FleetProvisioningDev/provision/*"
        ]
      }
    ]
  })
}

resource "aws_iot_policy" "device_policy" {
  name = var.device_policy_name

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Condition = {
          Bool = {
            "iot:Connection.Thing.IsAttached" = "true"
          }
        }
        Effect   = "Allow"
        Action   = "iot:Connect"
        Resource = "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:client/$${iot:Connection.Thing.ThingName}"
      },
      {
        Effect = "Allow"
        Action = "iot:Publish"
        Resource = [
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/rules/UploadImages",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/things/$${iot:Connection.Thing.ThingName}/certificates/+/json",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/things/$${iot:Connection.Thing.ThingName}/jobs/+/update",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/things/$${iot:Connection.Thing.ThingName}/streams/+/get/json"
        ]
      },
      {
        Effect = "Allow"
        Action = "iot:Subscribe"
        Resource = [
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topicfilter/$aws/things/$${iot:Connection.Thing.ThingName}/jobs/notify-next",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topicfilter/things/$${iot:Connection.Thing.ThingName}/certificates/+/json/accepted",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topicfilter/things/$${iot:Connection.Thing.ThingName}/certificates/+/json/rejected",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topicfilter/$aws/things/$${iot:Connection.Thing.ThingName}/streams/+/data/json",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topicfilter/$aws/things/$${iot:Connection.Thing.ThingName}/streams/+/rejected/json"
        ]
      },
      {
        Effect = "Allow"
        Action = "iot:Receive"
        Resource = [
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/things/$${iot:Connection.Thing.ThingName}/jobs/notify-next",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/things/$${iot:Connection.Thing.ThingName}/certificates/+/json/+",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/things/$${iot:Connection.Thing.ThingName}/streams/+/data/json",
          "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:topic/$aws/things/$${iot:Connection.Thing.ThingName}/streams/+/rejected/json"
        ]
      }
    ]
  })
}

# Define local variable with the list of AWS-managed policies
locals {
  iot_managed_policies = [
    "arn:aws:iam::aws:policy/service-role/AWSIoTLogging",
    "arn:aws:iam::aws:policy/service-role/AWSIoTRuleActions",
    "arn:aws:iam::aws:policy/service-role/AWSIoTThingsRegistration"
  ]
}

# IAM role for IoT provisioning
resource "aws_iam_role" "iot_provisioning_role" {
  name = var.iot_provisioning_role_name
  assume_role_policy = jsonencode({
    Version = "2012-10-17",
    Statement = [
      {
        Effect = "Allow",
        Principal = {
          Service = "iot.amazonaws.com"
        },
        Action = "sts:AssumeRole"
      }
    ]
  })
}

# Dynamic block to attach each policy in the list to the IAM role
resource "aws_iam_role_policy_attachment" "iot_managed_policy_attachments" {
  for_each = toset(local.iot_managed_policies)

  role       = aws_iam_role.iot_provisioning_role.name
  policy_arn = each.value
}

resource "aws_iot_provisioning_template" "template" {
  name                  = "${var.template_name}-${var.environment}"
  description           = var.template_description
  provisioning_role_arn = aws_iam_role.iot_provisioning_role.arn
  enabled               = true

  template_body = jsonencode({
    Parameters = {
      MacAddress                  = { Type = "String" }
      "AWS::IoT::Certificate::Id" = { Type = "String" }
    }

    Resources = {
      thing = {
        Type = "AWS::IoT::Thing"
        Properties = {
          AttributePayload = {}
          ThingGroups      = ["${var.environment}"]
          ThingName = {
            "Fn::Join" = [
              "",
              [
                "esp32-",
                { Ref = "MacAddress" }
              ]
            ]
          }
        }
      }

      certificate = {
        Type = "AWS::IoT::Certificate"
        Properties = {
          CertificateId = { Ref = "AWS::IoT::Certificate::Id" }
          Status        = "Active"
        }
      }

      policy = {
        Type = "AWS::IoT::Policy"
        Properties = {
          PolicyName = "${var.device_policy_name}"
        }
      }
    }
  })

  tags = {
    environment = var.environment
  }
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE A DYNAMODB TABLE FOR STORING DEVICE'S MAC ADDRESS
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_dynamodb_table" "onboarding_devices" {
  name                        = var.dynamodb_table_name
  billing_mode                = var.billing_mode
  read_capacity               = var.read_capacity
  write_capacity              = var.write_capacity
  deletion_protection_enabled = true
  table_class                 = "STANDARD"
  stream_enabled              = false

  hash_key  = "MacAddress"
  range_key = var.environment

  attribute {
    name = "MacAddress"
    type = "S"
  }

  attribute {
    name = var.environment
    type = "S"
  }

  point_in_time_recovery {
    enabled = false
  }

  ttl {
    enabled = false
  }
  # Optional tags
  tags = {
    environment = var.environment
  }
}