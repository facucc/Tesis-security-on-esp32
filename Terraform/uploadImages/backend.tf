terraform {
  backend "s3" {
    bucket  = "tesis-iot-terraform-state"
    key     = "state/upload_images/terraform.tfstate"
    region  = "us-east-1"
    encrypt = true
  }
}