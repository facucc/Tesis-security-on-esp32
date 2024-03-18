output "client_id" {
  description = "Cliend ID"
  value       = aws_iot_thing.iot_esp_32.default_client_id
}