terraform {
  backend "s3" {
    bucket  = "tesis-iot-terraform-state"
    key     = "state/fleet_provisioning_validation/terraform.tfstate"
    region  = "us-east-1"
    encrypt = true
  }
}