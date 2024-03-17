output "client_id" {
  value = aws_iot_thing.iot_esp_32.default_client_id
}

output "cert_pem" {
  value = aws_iot_certificate.iot_certificate.certificate_pem
}

output "public_key" {
  value = aws_iot_certificate.iot_certificate.public_key
}

output "private_key" {
  value = aws_iot_certificate.iot_certificate.private_key
  sensitive = true
}