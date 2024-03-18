terraform {
 backend "s3" {
   bucket         = "tesis-iot-terraform-state"
   key            = "state/terraform.tfstate"
   region         = "us-east-1"
   encrypt        = true
 }
}