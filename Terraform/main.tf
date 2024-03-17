terraform {
    required_version = ">= 1.0.0"
    required_providers {
        aws = {
        source  = "hashicorp/aws"
        version = "5.41.0"
        }
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