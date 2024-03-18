terraform {
  required_version = ">= 1.0.0"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "5.41.0"
    }
  }
}

provider "aws" {
  region = "us-east-1"
}

locals {
  cert_pem_path    = "${path.module}/../Certificates/certificate.pem.crt"
  private_key_path = "${path.module}/../Certificates/private.pem.key"
  public_key_path  = "${path.module}/../Certificates/public_key.pem"

  default_aws_iot_policies = {
    "AmazonFreeRTOSOTAUpdate"  = "arn:aws:iam::aws:policy/service-role/AmazonFreeRTOSOTAUpdate",
    "AWSIoTLogging"            = "arn:aws:iam::aws:policy/service-role/AWSIoTLogging",
    "AWSIoTRuleActions"        = "arn:aws:iam::aws:policy/service-role/AWSIoTRuleActions",
    "AWSIoTThingsRegistration" = "arn:aws:iam::aws:policy/service-role/AWSIoTThingsRegistration",

  }
}

#AWS-IoT-Thing
resource "aws_iot_thing" "iot_esp_32" {
  name = "test"
  attributes = {
    Environment = "dev"
  }
}

#AWS-IoT-policy
resource "aws_iot_policy" "iot_esp_32" {
  name = "esp-32-test"

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Action = [
          "iot:*",
        ]
        Effect   = "Allow"
        Resource = "*"
      },
    ]
  })
}
#AWS-IoT-certificates
resource "aws_iot_certificate" "iot_certificate" {
  active = true

  depends_on = [
    aws_iot_thing.iot_esp_32
  ]
}

#AWS-IoT-certificates-attachment
resource "aws_iot_thing_principal_attachment" "iot_attachment" {
  principal = aws_iot_certificate.iot_certificate.arn
  thing     = aws_iot_thing.iot_esp_32.name
}


#AWS-IoT-policy-Attachment
resource "aws_iot_policy_attachment" "iot_policy_attachment" {
  policy = aws_iot_policy.iot_esp_32.name
  target = aws_iot_certificate.iot_certificate.arn
}
#bucket to save the code
resource "aws_s3_bucket" "firmware" {
  bucket = "firmware-tesis"

  tags = {
    Environment = "Dev"
  }
}

data "aws_iam_policy_document" "aws_iot_job_manager" {
  statement {
    effect  = "Allow"
    actions = ["sts:AssumeRole"]

    principals {
      type        = "Service"
      identifiers = ["iot.amazonaws.com"]
    }
  }
}
data "aws_iam_policy_document" "aws_iot_job_manager_custom" {

  statement {
    effect = "Allow"
    actions = [
      "s3:GetObjectVersion",
      "s3:GetObject",
      "s3:PutObject"
    ]

    resources = ["${aws_s3_bucket.firmware.arn}/*"]
  }
}
resource "aws_iam_role" "aws_iot_job_manager" {
  name               = "aws_iot_job_manager"
  assume_role_policy = data.aws_iam_policy_document.aws_iot_job_manager.json

  lifecycle {
    create_before_destroy = true
  }
}
resource "aws_iam_policy" "aws_iot_job_manager_custom" {
  name   = "aws_iot_job_manager_custom"
  policy = data.aws_iam_policy_document.aws_iot_job_manager_custom.json
}

resource "aws_iam_role_policy_attachment" "iot_policies_aws_managed" {
  for_each = {
    for policy_name, policy_arn in local.default_aws_iot_policies : policy_name => policy_arn
  }

  policy_arn = each.value
  role       = aws_iam_role.aws_iot_job_manager.name

  lifecycle {
    create_before_destroy = true
  }
}
resource "aws_iam_role_policy_attachment" "iot_policy_custom" {

  policy_arn = aws_iam_policy.aws_iot_job_manager_custom.arn
  role       = aws_iam_role.aws_iot_job_manager.name

  lifecycle {
    create_before_destroy = true
  }

}
# AWS IOT JOB Resources

# save local file
resource "local_file" "cert_pem" {
  filename = local.cert_pem_path
  content  = aws_iot_certificate.iot_certificate.certificate_pem
  lifecycle {
    ignore_changes = [content]
  }
}

resource "local_file" "private_key" {
  filename = local.private_key_path
  content  = aws_iot_certificate.iot_certificate.private_key
  lifecycle {
    ignore_changes = [content]
  }
}

resource "local_file" "public_key" {
  filename = local.public_key_path
  content  = aws_iot_certificate.iot_certificate.public_key
  lifecycle {
    ignore_changes = [content]
  }
}