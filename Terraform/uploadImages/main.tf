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
# CREATE THE IMAGES UPLOAD LAMBDA FUNCTION
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
      BUCKET_NAME = var.bucket_name
      TZ          = var.time_zone
      environment = var.environment
    }
  }
}

data "archive_file" "lambda" {
  type        = "zip"
  source_file = var.filename_path
  output_path = "lambda_function.zip"
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE AN IOT RULE TO TRIGGER THE IMAGES UPLOAD LAMBDA FUNCTION
# ---------------------------------------------------------------------------------------------------------------------


resource "aws_iot_topic_rule" "rule" {
  name        = var.rule_name
  description = "Rule to trigger the upload images lambda function"
  enabled     = true
  sql         = "SELECT * FROM '$aws/rules/UploadImages'"
  sql_version = "2016-03-23"

  lambda {
    function_arn = aws_lambda_function.lambda_function.arn

  }
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE A BUCKET FOR STORING IMAGES
# ---------------------------------------------------------------------------------------------------------------------

resource "aws_s3_bucket" "images" {
  bucket = var.bucket_name

  tags = {
    environment = var.environment
  }
}

# ---------------------------------------------------------------------------------------------------------------------
# CREATE AN IAM ROLE FOR THE CERTIFICATE REVOCATION LAMBDA FUNCTION
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
      "s3:PutObject",
    ]

    resources = ["arn:aws:s3:::${var.bucket_name}/*"]

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