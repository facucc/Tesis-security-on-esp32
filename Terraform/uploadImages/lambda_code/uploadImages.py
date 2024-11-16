import boto3 
import json
import os
import base64
from datetime import datetime

def lambda_handler(event, context):
    try:
        bucket_name = os.environ['BUCKET_NAME']
        
        if 'image' not in event:
            return {
                'statusCode': 400,
                'body': json.dumps('Request must include image.')
            }
        
        file_content = event['image']
        print(file_content)
        image_body = base64.b64decode(file_content)
        
        timestamp = datetime.now().strftime('%Y%m%d-%H%M%S')
        
        file_name = f"image-{timestamp}.jpg"
        
        s3 = boto3.client('s3')
        s3.put_object(Body=image_body, Bucket=bucket_name, Key=file_name)
        
        return {
            'statusCode': 200,
            'body': json.dumps(f'File {file_name} uploaded successfully.')
        }
    
    except Exception as e:
        return {
            'statusCode': 400,
            'body': json.dumps(f'Error uploading file: {str(e)}')
        }
