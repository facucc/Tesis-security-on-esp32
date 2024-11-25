terraform {
  backend "s3" {
    bucket  = "tesis-iot-terraform-state"
    key     = "state/certificate_renewal/terraform.tfstate"
    region  = "us-east-1"
    encrypt = true
  }
}