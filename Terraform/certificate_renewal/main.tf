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
  region = "us-east-1"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE THE CERTIFICATE RENEWAL LAMBDA FUNCTION 
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_lambda_function" "cert_renewal_lambda_function" {
  function_name = var.cert_renewal_function_name
  description   = var.cert_renewal_description
  runtime       = var.runtime
  handler       = var.cert_renewal_handler
  role          = aws_iam_role.cert_renewal_lambda_function_role.arn
  memory_size   = var.memory_size
  timeout       = var.timeout
  publish       = true

  filename         = data.archive_file.cert_renewal_lambda.output_path
  source_code_hash = data.archive_file.cert_renewal_lambda.output_base64sha256


  environment {
    variables = {
      ACCEPTED_TOPIC = var.accepted_topic
      REJECTED_TOPIC = var.rejected_topic
      environment    = var.environment
    }
  }
}

data "archive_file" "cert_renewal_lambda" {
  type        = "zip"
  source_file = var.cert_renewal_filename_path
  output_path = "cert_renewal_lambda_function.zip"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE AN IOT RULE TO TRIGGER THE CERTIFICATE RENEWAL LAMBDA FUNCTION
# ---------------------------------------------------------------------------------------------------------------------


resource "aws_iot_topic_rule" "rule" {
  name        = var.rule_name
  description = "Rule to trigger the certificate renewal lambda function"
  enabled     = true
  sql         = "SELECT *, topic(2) as thingName FROM 'things/+/certificate/create-from-csr/json'"
  sql_version = "2016-03-23"

  lambda {
    function_arn = aws_lambda_function.cert_renewal_lambda_function.arn

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

# ---------------------------------------------------------------------------------------------------------------------
# CREATE AN IAM ROLE FOR THE CERTIFICATE RENEWAL LAMBDA FUNCTION
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_iam_role" "cert_renewal_lambda_function_role" {
  name               = var.cert_renewal_function_name
  description        = "For lambda function ${var.cert_renewal_function_name}"
  assume_role_policy = data.aws_iam_policy_document.lambda_role.json

  lifecycle {
    create_before_destroy = true
  }
}

# Create a custom IAM policy for IoT permissions based on the defined policy document
resource "aws_iam_policy" "cert_renewal_iot_policy" {
  name   = var.cert_renewal_function_name
  policy = data.aws_iam_policy_document.cert_renewal_iot_policy.json
  lifecycle {
    create_before_destroy = true
  }
}

# Define the IoT policy document with necessary permissions
data "aws_iam_policy_document" "cert_renewal_iot_policy" {
  statement {
    actions = [
      "iot:CreateCertificateFromCsr",
      "iot:DescribeThing",
      "iot:ListThingPrincipals",
      "iot:ListAttachedPolicies",
      "iot:AttachPolicy",
      "iot:AttachThingPrincipal",
      "iot:Publish",
    ]

    resources = ["*"]

    effect = "Allow"
  }
}

# Attach the custom IoT policy to the Lambda role
resource "aws_iam_role_policy_attachment" "cert_renewal_iot_policy_attachment" {
  role       = aws_iam_role.cert_renewal_lambda_function_role.name
  policy_arn = aws_iam_policy.cert_renewal_iot_policy.arn
}

# Attach CloudWatch logging policy to the Lambda role for basic execution permissions
resource "aws_iam_role_policy_attachment" "logging_for_cert_renewal_lambda" {
  role       = aws_iam_role.cert_renewal_lambda_function_role.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE A BUCKET FOR STORING JOB DOCUMENTS
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_s3_bucket" "job_document" {
  bucket = var.bucket_name

  tags = {
    environment = var.environment
  }
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE THE CERTIFICATE RENEWAL REQUEST LAMBDA FUNCTION 
# ---------------------------------------------------------------------------------------------------------------------


data "aws_caller_identity" "current" {}

# Lambda function for Request Renewal
resource "aws_lambda_function" "cert_renewal_request_lambda_function" {
  function_name = var.cert_renewal_request_function_name
  description   = var.cert_renewal_request_description
  runtime       = var.runtime
  handler       = var.cert_renewal_request_handler
  role          = aws_iam_role.cert_renewal_request_lambda_function_role.arn
  memory_size   = var.memory_size
  timeout       = var.timeout
  publish       = true

  filename         = data.archive_file.cert_renewal_request_lambda.output_path
  source_code_hash = data.archive_file.cert_renewal_request_lambda.output_base64sha256

  environment {
    variables = {
      AWS_ACCOUNT_ID      = data.aws_caller_identity.current.account_id
      JOB_DOCUMENT_SOURCE = var.bucket_name
      environment         = var.environment
    }
  }
}

data "archive_file" "cert_renewal_request_lambda" {
  type        = "zip"
  source_file = var.cert_renewal_request_filename_path
  output_path = "cert_renewal_request_lambda_function.zip"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE AN IAM ROLE FOR THE CERTIFICATE RENEWAL REQUEST LAMBDA FUNCTION
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_iam_role" "cert_renewal_request_lambda_function_role" {
  name               = var.cert_renewal_request_function_name
  description        = "For lambda function ${var.cert_renewal_request_function_name}"
  assume_role_policy = data.aws_iam_policy_document.lambda_role.json

  lifecycle {
    create_before_destroy = true
  }
}

# Create a custom IAM policy for IoT permissions based on the defined policy document
resource "aws_iam_policy" "cert_renewal_request_iot_policy" {
  name   = var.cert_renewal_request_function_name
  policy = data.aws_iam_policy_document.cert_renewal_request_iot_policy.json
}

# Define the IoT policy document with necessary permissions
data "aws_iam_policy_document" "cert_renewal_request_iot_policy" {
  statement {
    effect = "Allow"
    actions = [
      "iot:CreateJob"
    ]
    resources = [
      "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:job/*"
    ]
  }

  statement {
    effect = "Allow"
    actions = [
      "iot:DescribeThingGroup",
      "iot:ListThingGroups"
    ]
    resources = [
      "arn:aws:iot:${var.region}:${data.aws_caller_identity.current.account_id}:thinggroup/*"
    ]
  }

  statement {
    effect = "Allow"
    actions = [
      "s3:GetObject"
    ]
    resources = [
      "arn:aws:s3:::${var.bucket_name}/jobDocument.json"
    ]
  }
}

# Attach the custom IoT policy to the Lambda role
resource "aws_iam_role_policy_attachment" "cert_renewal_request_iot_policy_attachment" {
  role       = aws_iam_role.cert_renewal_request_lambda_function_role.name
  policy_arn = aws_iam_policy.cert_renewal_request_iot_policy.arn
}

# Attach CloudWatch logging policy to the Lambda role for basic execution permissions
resource "aws_iam_role_policy_attachment" "logging_for_cert_renewal_request_lambda" {
  role       = aws_iam_role.cert_renewal_request_lambda_function_role.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}