#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

#include "ota_agent.h"
#include <freertos/FreeRTOS.h>

#define PATCH_FILE_EXTENSION ".patch"
#define PATCH_BUFFER_SIZE    4096
#define PATCH_PARTITION_NAME "ota_patch"

static const char * TAG = "OTA_AGENT";

#define JANPATCH_STREAM esp_partition_context_t
#include "janpatch.h"


static bool prvIsPatchFile( const char *pFilePath );
static bool prvCreateOtaFile(esp_ota_context_t * ota_ctx);
static bool prvCreatePatchFile( esp_ota_context_t * ota_ctx );
static int prvfseek( esp_partition_context_t *fileCtx, long int offset, int whence );
static size_t prvfread( void *buffer, size_t size, size_t count, esp_partition_context_t *pCtx );
static size_t prvfwrite( const void *buffer, size_t size, size_t count, esp_partition_context_t *pCtx );
static long int prvftell( esp_partition_context_t *pCtx );

bool ApplyPatch( esp_ota_context_t * ota_ctx)
{
    bool xReturn = 0;

    esp_partition_context_t sourceCtx = { 0 }, patchCtx = { 0 }, targetCtx = { 0} ;
    unsigned char *source_buffer = NULL, *patch_buffer = NULL, *target_buffer = NULL;

    /* Set source partition context.*/
    sourceCtx.partition = esp_ota_get_running_partition();
    sourceCtx.size = sourceCtx.partition->size;
    source_buffer = (unsigned char *) pvPortMalloc( PATCH_BUFFER_SIZE );

    if ( source_buffer == NULL )
    {
        ESP_LOGE(TAG, "pvPortMalloc failed allocating %d bytes for source_buffer.", PATCH_BUFFER_SIZE );
        return 0;
    }

    /* Set desitination partition context. */
    patchCtx.partition = ota_ctx->patch_partition;
    patchCtx.size = ota_ctx->data_write_len;
    patch_buffer = (unsigned char *)pvPortMalloc( PATCH_BUFFER_SIZE );
    if ( patch_buffer == NULL )
    {
        ESP_LOGE(TAG, "pvPortMalloc failed allocating %d bytes for patch_buffer.", PATCH_BUFFER_SIZE );
        free( source_buffer );
        return 0;
    }

    /* Set target partition context. */
    targetCtx.partition = ota_ctx->update_partition;
    target_buffer = (unsigned char *)pvPortMalloc( PATCH_BUFFER_SIZE );
    if ( target_buffer == NULL )
    {
        ESP_LOGE(TAG, "pvPortMalloc failed allocating %d bytes for target_buffer.", PATCH_BUFFER_SIZE );
        free( source_buffer );
        free( patch_buffer );
        return 0;
    }

     /* Set janpatch context.*/
    janpatch_ctx jCtx =
    {
        { source_buffer, PATCH_BUFFER_SIZE, 0xffffffff, 0, NULL, 0},
        { patch_buffer,  PATCH_BUFFER_SIZE, 0xffffffff, 0, NULL, 0} ,
        { target_buffer, PATCH_BUFFER_SIZE, 0xffffffff, 0, NULL, 0},
        &prvfread,
        &prvfwrite,
        &prvfseek,
        &prvftell,
        //&prvPatchProgress,
        NULL,
        0
    };

    /* Patch the base version. */
    xReturn = janpatch( jCtx, &sourceCtx, &patchCtx, &targetCtx );
    if( xReturn == 0 )
    {
        ota_ctx->data_write_len = targetCtx.offset;
    }

    /* Release the buffers. */
    free( source_buffer );
    free( patch_buffer );
    free( target_buffer );

    return ( xReturn == 0 );
}
/* Returns true if the file recieved is delta, false otherwise. */
static bool prvIsPatchFile( const char *pFilePath )
{
    
    const char * patchFileExt = strrchr( pFilePath, '.' );
    bool xReturn = false;

    if ( patchFileExt != NULL )
    { 
        /* Check if the file is a patch file. */
        if ( !strncmp( patchFileExt, PATCH_FILE_EXTENSION , sizeof( PATCH_FILE_EXTENSION ) ) )
        {
            xReturn = true;
            ESP_LOGI(TAG,"Received file is a patch." );
        }

    }
    else
    {
        ESP_LOGE(TAG,"No extension in the received filename. OTA full file" );
    }

    return xReturn;
}

bool SetOTAUpdateContext( const char * filePath, esp_ota_context_t * ota_ctx)
{
    bool xReturn = false;
    memset(ota_ctx, '0', sizeof(esp_ota_context_t));

    /* Check if the file is a patch. */
    ESP_LOGE(TAG, "FILE PATH %s", filePath);

    if ( prvIsPatchFile( filePath ) )
    {
        /* Create a patch file. */                
        xReturn = prvCreatePatchFile( ota_ctx );
    }
    else
    {
        /* Create an ota file. */
        xReturn = prvCreateOtaFile( ota_ctx );
    }

    return xReturn;
}

/* Create file for storing the full ota image. */
static bool prvCreateOtaFile(esp_ota_context_t * ota_ctx)
{
    esp_ota_handle_t update_handle;
    esp_err_t err;

    const esp_partition_t * update_partition = esp_ota_get_next_update_partition( NULL );

    if( update_partition == NULL )
    {
        ESP_LOGE( TAG, "Failed to find update partition. \n" );
        return false;
    }

    ESP_LOGI( TAG, "Writing to partition subtype %d at offset 0x%"PRIx32"",
               update_partition->subtype, update_partition->address );

    
    err = esp_ota_begin( update_partition, OTA_SIZE_UNKNOWN, &update_handle );

    if( err != ESP_OK )
    {
        ESP_LOGE( TAG, "esp_ota_begin failed (%d)", err  );
        return false;
    }

    ota_ctx->update_partition = update_partition;
    ota_ctx->update_handle = update_handle;
    ota_ctx->OtaPartition_type = OtaUpdatePartition;
    ota_ctx->valid_image = false;   

    ESP_LOGI( TAG, "esp_ota_begin succeeded" );
    return true;
}
/* Create file for storing patch data. */
static bool prvCreatePatchFile( esp_ota_context_t * ota_ctx )
{
    bool xReturn = false;

    const esp_partition_t *patch_partition = NULL;

    /* Find the OTA patch partiton. */
    patch_partition = esp_partition_find_first( ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, PATCH_PARTITION_NAME );

    if( !prvCreateOtaFile( ota_ctx ))
    {
        ESP_LOGE( TAG, "Failed to set update partition. \n" );
        return false;
    }

    if ( patch_partition != NULL) 
    {
        ESP_LOGI( TAG, "Found %s partition.", PATCH_PARTITION_NAME );

        /* Erase the partiton. */
        esp_partition_erase_range( patch_partition, 0, patch_partition->size );        

        /* Set ota context. */
        ota_ctx->patch_partition   = patch_partition;
        ota_ctx->OtaPartition_type = OtaPatchPartition;
        ota_ctx->data_write_len    = 0;
        ota_ctx->valid_image       = false;
        

        xReturn = true;
    }
    else
    {
        ESP_LOGE( TAG, "Could not find %s partition.", PATCH_PARTITION_NAME );

        xReturn = false;
    }

    return xReturn;
}

/* Sets the offset for the partition to the value pointed to by offset. */
static int prvfseek( esp_partition_context_t *fileCtx, long int offset, int whence )
{
    switch ( whence )
    {
        case SEEK_SET:
        { 
            fileCtx->offset = offset;
            break;
        }

        case SEEK_CUR:
        {
            fileCtx->offset += offset;
            break;
        }

        case SEEK_END:
        {
            fileCtx->offset = fileCtx->size + offset;
            break;
        }

        default:
            return -1;
    }

    if ( fileCtx->offset > fileCtx->size )
    {
        return -1;
    }
        
    return 0;
}

/* Read block of data from partition. */
static size_t prvfread( void *buffer, size_t size, size_t count, esp_partition_context_t *pCtx )
{
    esp_err_t esp_ret = ESP_FAIL;
    
    esp_ret = esp_partition_read( pCtx->partition, pCtx->offset, buffer, size * count );

    if ( esp_ret != ESP_OK )
    {
        ESP_LOGE(TAG,"esp_partition_read error: %d\n", esp_ret );
        return 0;
    }
    int new_pos = pCtx->offset + ( size * count );

    if ( new_pos > pCtx->size )
    {
        int ret = pCtx->size - pCtx->offset;
        pCtx->offset = pCtx->size;
        return ret;
    }

    /* Udpate offset.*/
    pCtx->offset = new_pos;

    return ( size * count );
}

/* Write block of data to partition. */
static size_t prvfwrite( const void *buffer, size_t size, size_t count, esp_partition_context_t *pCtx )
{
    esp_err_t esp_ret = ESP_FAIL;

    ESP_LOGE(TAG, "Tipo: 0x%x\n", pCtx->partition->type);
    ESP_LOGE(TAG, "Subtipo: 0x%x\n", pCtx->partition->subtype);
    ESP_LOGE(TAG, "Dirección de comienzo: 0x%lx\n", pCtx->partition->address);

    esp_ret = esp_partition_write( pCtx->partition, pCtx->offset, buffer, size * count );

    //esp_ret = esp_ota_write_with_offset( ota_ctx.update_handle, data, dataLength, totalBytesReceived );

    if ( esp_ret != ESP_OK )
    {
        ESP_ERROR_CHECK(esp_ret);
        ESP_LOGE(TAG, "esp_partition_write error: %d\n", esp_ret);
        return 0;
    }

    /* Udpate offset. */
    pCtx->offset += ( size * count );

    return ( size * count );
}

/* Get current offset in partition. */
static long int prvftell( esp_partition_context_t *pCtx )
{
    return pCtx->offset;
}