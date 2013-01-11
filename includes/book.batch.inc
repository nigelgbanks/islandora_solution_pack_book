<?php

/**
 * @file
 *
 * Provides several batch operations for manipulating books.
 *
 * Each batch operation deals with a single page at a time.
 *
 * Batches provided:
 *   Create Book PDF
 *   Reorder Pages
 *   Delete Pages
 */

/**
 * Sets up a batch operation.
 *
 * @param array $pages
 *   All the pages to operate on.
 * @param array $context
 *   The context of this batch operation.
 * @param array $message
 *   Values are used as parameters to t() and drupal_set_message() that will be
 *   displayed when the batch is finished.
 *
 *   Placeholders include, but are not limited to:
 *     @successful, @failed, @total.
 *
 * @return FedoraObject $page
 *   The current page to operate on.
 */
function islandora_book_start_batch_operation(array $operands, array &$context, $message = array()) {
  if (!isset($context['sandbox']['progress'])) {
    if (empty($context['results'])) {
      // "files" will be deleted upon failure or success of the batch.
      // "successful" an array of boolean values indicating operation success.
      // "messages" array of messages to display on finishing the batch
      //    values are used as parameters to t() and drupal_set_message().
      $context['results'] = array_fill_keys(array('files', 'successful', 'messages'), array());
    }
    $context['sandbox']['progress'] = 0;
    $context['sandbox']['total'] = count($operands);
    $context['results']['messages'][] = $message;
  }
  $operand_index = $context['sandbox']['progress'];
  return $operands[$operand_index];
}

/**
 * Ends the batch operation.
 *
 * @param array $context
 *   The context of this batch operation.
 * @param int $count
 *   The number of steps taken in the operation, typically 1.
 * @param array $message
 *   Values are used as parameters to t() and drupal_set_message() that will be
 *   displayed when the batch is finished.
 *
 *   Placeholders include, but are not limited to:
 *     @successful, @failed, @total.
 *
 * @return boolean
 *   TRUE if the batch operation is complete, FALSE otherwise.
 */
function islandora_book_end_batch_operation(array &$context, $count = 1, $message = array()) {
  $context['sandbox']['progress'] += $count;
  if ($context['sandbox']['progress'] != $context['sandbox']['total']) {
    $context['finished'] = $context['sandbox']['progress'] / $context['sandbox']['total'];
    return FALSE;
  }
  else {
    $context['results']['messages'][] = $message;
  }
  return TRUE;
}

/**
 * The batch 'finished' callback.
 */
function islandora_book_batch_finished($success, $results, $operations) {
  if ($success) {
    // Delete any lingering files.
    array_walk($results['files'], function($o) {
        if (file_exists($o)) {
          file_unmanaged_delete($o);
        }
      });
    // Build generic place holders for the message.
    $total = count($results['successful']);
    $successful = count(array_filter($results['successful']));
    $failed = $total - $successful;
    $place_holders = array(
      '@successful' => $successful,
      '@failed' => $failed,
      '@total' => $total
    );
    // Display all messages/errors/warnings.
    $messages = array_filter($results['messages']);
    array_walk($messages, function($o) use ($place_holders) {
        $msg = $o[0];
        $vars = isset($o[1]) ? array_merge($o[1], $place_holders) : $place_holders;
        $status = isset($o[2]) ? $o[2] : 'status';
        drupal_set_message(t($msg, $vars), $status);
      });
  }
  else {
    $error_operation = reset($operations);
    $message = t('An error occurred while processing %error_operation with arguments: @arguments', array(
                 '%error_operation' => $error_operation[0],
                 '@arguments' => print_r($error_operation[1], TRUE)));
    drupal_set_message($message, 'error');
  }
}

/**
 * Gets the batch definition for creating a PDF from the given pages from the
 * given book. This also updates each page object with a corresponding pdf.
 *
 * @param FedoraObject $object
 *   The object the pages belong to.
 * @param array $pages
 *   All the pages to include in the PDF.
 * @param array $options
 *   Options to pass into convert.
 *
 * @return array
 *   The batch definition.
 */
function islandora_book_create_pdf_batch(FedoraObject $object, array $pages, array $options) {
  return array(
    'operations' => array(
      array('islandora_book_create_pdf_page_batch_operation', array($pages, $options)),
      array('islandora_book_create_pdf_book_batch_operation', array($object)),
    ),
    'finished' => 'islandora_book_batch_finished',
    'title' => t('Creating PDF from @label', array('@label' => $object->label)),
    'init_message' => t('Preparing to create PDF from @label.', array('@label' => $object->label)),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occured.'),
    'file' => drupal_get_path('module', 'islandora_book') . '/includes/book.batch.inc',
  );
}

/**
 * Batch operation for converting a page into a PDF.
 *
 * @param array $pages
 *   All the pages to prepare.
 * @param array $options
 *   Options to pass into convert.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_book_create_pdf_page_batch_operation(array $pages, array $options, array &$context) {
  module_load_include('inc', 'islandora_book', 'includes/utilities');
  $page = islandora_book_start_batch_operation($pages, $context);
  $object = islandora_object_load($page);
  $pdf_file = islandora_book_page_derive_pdf($object, $options);
  $context['results']['files'][] = $pdf_file;
  $context['results']['pages'][] = $pdf_file;
  islandora_book_update_datastream($object, $pdf_file, 'PDF');
  islandora_book_end_batch_operation($context);
  $context['message'] = t('Converting page @page of @total into a PDF', array(
                          '@page' => $context['sandbox']['progress'],
                          '@total' => $context['sandbox']['total']));
}

/**
 * Batch operation for combining PDF files.
 *
 * @param FedoraObject $object
 *   The object to store the PDF file.
 * @param string $options
 *   Options to pass into convert.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_book_create_pdf_book_batch_operation(FedoraObject $object, array &$context) {
  module_load_include('inc', 'islandora_book', 'includes/utilities');
  islandora_book_start_batch_operation($context['results']['pages'], $context);
  $pdf_file = drupal_realpath("temporary://{$object->id}_PDF.pdf");
  $files = array_slice($context['results']['pages'], $progress, 10);
  if ($context['sandbox']['progress'] != 0) {
    islandora_book_pdf_append($pdf_file, $files);
  }
  else {
    islandora_book_pdf_combine($files, $pdf_file);
  }
  $finished_message = array('Created PDF with @pages pages.', array('@pages' => count($context['results']['pages'])));
  if (islandora_book_end_batch_operation($context, count($files), $finished_message)) {
    islandora_book_update_datastream($object, $pdf_file, 'PDF');
    $context['results']['files'][] = $pdf_file;
  }
  $context['message'] = t('Adding page @page of @total to the PDF', array('@page' => $context['sandbox']['progress'], '@total' => $context['sandbox']['total']));
}

/**
 * Gets the batch definition for creating OCR, RAW_HOCR, and ENCODED_OCR
 * datastreams in each of the given page objects.
 *
 * @param FedoraObject $object
 *   The object the pages belong to.
 * @param array $pages
 *   All the pages to process.
 * @param array $options
 *   Options to pass into tesseract/gimp.
 *
 * @return array
 *   The batch definition.
 */
function islandora_book_create_ocr_batch(FedoraObject $object, array $pages, array $options) {
  return array(
    'operations' => array(
      array('islandora_book_create_ocr_batch_operation', array($pages, $options)),
    ),
    'finished' => 'islandora_book_batch_finished',
    'title' => t('Creating PDF from @label', array('@label' => $object->label)),
    'init_message' => t('Preparing to create PDF from @label.', array('@label' => $object->label)),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_book') . '/includes/book.batch.inc',
  );
}

/**
 * Batch operation for deriving a pages OCR datastreams.
 *
 * @param array $pages
 *   All the pages to prepare.
 * @param array $options
 *   Options to pass into convert.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_book_create_ocr_batch_operation(array $pages, array $options, array &$context) {
  module_load_include('inc', 'islandora_book', 'includes/utilities');
  $page = islandora_book_start_batch_operation($pages, $context);
  $object = islandora_object_load($page);
  $context['results']['successful'][] = islandora_book_page_derive_ocr_datastreams($object, $options);
  $finished_message = array('Performed OCR on @successful of @total pages.');
  islandora_book_end_batch_operation($context, 1, $finished_message);
  $context['message'] = t('Performing OCR on page @page of @total.', array(
                          '@page' => $context['sandbox']['progress'],
                          '@total' => $context['sandbox']['total']));
}

/**
 * Gets the batch definition for creating TN, JPEG, JP2
 * datastreams in each of the given page objects.
 *
 * @param FedoraObject $object
 *   The object the pages belong to.
 * @param array $pages
 *   All the pages to process.
 *
 * @return array
 *   The batch definition.
 */
function islandora_book_create_images_batch(FedoraObject $object, array $pages) {
  return array(
    'operations' => array(
      array('islandora_book_create_images_batch_operation', array($pages)),
    ),
    'finished' => 'islandora_book_batch_finished',
    'title' => t('Creating Images for @label', array('@label' => $object->label)),
    'init_message' => t('Preparing to create images for @label.', array('@label' => $object->label)),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occured.'),
    'file' => drupal_get_path('module', 'islandora_book') . '/includes/book.batch.inc',
  );
}

/**
 * Batch operation for creating image derivatives for a page.
 *
 * @param array $pages
 *   All the pages to prepare.
 * @param array $options
 *   Options to pass into convert.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_book_create_images_batch_operation(array $pages, array &$context) {
  module_load_include('inc', 'islandora_book', 'includes/utilities');
  $page = islandora_book_start_batch_operation($pages, $context);
  $object = islandora_object_load($page);
  $context['results']['successful'][] = islandora_book_page_derive_image_datastreams($object);
  $finished_message = array('Created images for @successful of @total pages.');
  islandora_book_end_batch_operation($context, 1, $finished_message);
  $context['message'] = t('Creating images for page @page of @total.', array(
                          '@page' => $context['sandbox']['progress'],
                          '@total' => $context['sandbox']['total']));
}

/**
 * Gets the batch definition for deleting the given pages from the given book.
 *
 * @param FedoraObject $object
 *   The object the pages belong to.
 * @param array $pages
 *   All the pages to delete.
 *
 * @return array
 *   The batch definition.
 */
function islandora_book_sequence_pages_batch(FedoraObject $object, array $pages) {
  return array(
    'operations' => array(
      array('islandora_book_sequence_pages_batch_operation', array($pages))
    ),
    'finished' => 'islandora_book_batch_finished',
    'title' => t('Sequencing @label\'s pages', array('@label' => $object->label)),
    'init_message' => t('Preparing to sequence @label\'s pages.', array('@label' => $object->label)),
    'progress_message' => t('Updated @current out of @total.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_book') . '/includes/book.batch.inc',
  );
}

/**
 * Changes the ordering of the pages.
 *
 * @param array $pages
 *   All the pages in the order they should be in.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_book_sequence_pages_batch_operation(array $pages, array &$context) {
  module_load_include('inc', 'islandora_book', 'includes/utilities');
  $page = islandora_book_start_batch_operation($pages, $context);
  $object = islandora_object_load($page);
  $position = &$context['sandbox']['progress'];
  $context['message'] = t('Inserting page "@label" (@pid) at position "@pos"', array(
                          '@label' => $object->label,
                          '@pid' => $object->id,
                          '@pos' => $position));
  islandora_book_set_relationship($object->relationships, ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', $position, TRUE);
  // Assume it worked.
  $context['results'][] = TRUE;
  islandora_book_end_batch_operation($context);
}

/**
 * Gets the batch definition for deleting the given pages from the given book.
 *
 * @param FedoraObject $object
 *   The object the pages belong to.
 * @param array $pages
 *   All the pages to delete.
 *
 * @return array
 *   The batch definition.
 */
function islandora_book_delete_pages_batch(FedoraObject $object, array $pages) {
  return array(
    'operations' => array(
      array('islandora_book_delete_pages_batch_operation', array($pages))
    ),
    'finished' => 'islandora_book_batch_finished',
    'title' => t('Deleting page(s) from @label', array('@label' => $object->label)),
    'init_message' => t('Preparing to delete page(s) from @label.', array('@label' => $object->label)),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaning @estimate.'),
    'error_message' => t('An error has occured.'),
    'file' => drupal_get_path('module', 'islandora_book') . '/includes/book.batch.inc',
  );
}

/**
 * Batch operation for deleting a page object.
 *
 * @param array $pages
 *   All the pages to delete.
 * @param array $context
 *   The context of this batch operation.
 */
function islandora_book_delete_pages_batch_operation(array $pages, array &$context) {
  $page = islandora_book_start_batch_operation($pages, $context);
  $object = islandora_object_load($page);
  $context['message'] = t('Deleting page "@label" (@pid)', array('@label' => $object->label, '@pid' => $object->id));
  $context['results']['successful'][] = islandora_delete_object($object);
  $finished_message = array('Deleted @successful pages.');
  islandora_book_end_batch_operation($context, 1, $finished_message);
}
